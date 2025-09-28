#include "AnimNode_VRMSpringBone.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "VRMInterchangeLog.h"

void FAnimNode_VRMSpringBone::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    FAnimNode_Base::Initialize_AnyThread(Context);
    ComponentPose.Initialize(Context);
    InvalidateCaches();
    TimeAccumulator = 0.f;
}

void FAnimNode_VRMSpringBone::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    ComponentPose.CacheBones(Context);

    const FBoneContainer& BoneContainer = Context.AnimInstanceProxy->GetRequiredBones();

    bool bNeedsRebuild = !bCachesValid;

    if (SpringConfig == nullptr)
    {
        if (bCachesValid)
        {
            InvalidateCaches();
        }
        return;
    }

    const FString CurrentHash = SpringConfig->GetEffectiveHash();
    if (!LastAssetPtr.IsValid() || LastAssetPtr.Get() != SpringConfig || LastAssetHash != CurrentHash)
    {
        bNeedsRebuild = true;
    }

    if (bNeedsRebuild)
    {
        RebuildCaches_AnyThread(BoneContainer);
    }
}

void FAnimNode_VRMSpringBone::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    ComponentPose.Update(Context);

    if (!SpringConfig || !SpringConfig->SpringConfig.IsValid())
    {
        return; // nothing to simulate
    }

    const float InputDelta = FMath::Min(Context.GetDeltaTime(), MaxDeltaTime);

    if (bUseFixedTimeStep)
    {
        TimeAccumulator += InputDelta;
        const float Step = FixedTimeStep > 0.f ? FixedTimeStep : (1.f/60.f);

        int32 StepsThisFrame = 0;
        while (TimeAccumulator + KINDA_SMALL_NUMBER >= Step && StepsThisFrame < MaxSubsteps)
        {
            TimeAccumulator -= Step;
            ++StepsThisFrame;
        }
        if (StepsThisFrame == 0)
        {
            CachedSubsteps = 0; // no simulation scheduled
            return;
        }
        CachedSubsteps = StepsThisFrame;
        CachedH = Step;
    }
    else
    {
        const int32 Substeps = FMath::Clamp(VariableSubsteps, 1, 32);
        CachedSubsteps = Substeps;
        CachedH = (Substeps > 0) ? (InputDelta / (float)Substeps) : InputDelta;
    }

#if !UE_BUILD_SHIPPING
    const uint64 FrameCounter = GFrameCounter;
    static uint64 LastStepLoggedFrame = (uint64)-1;
    if (LastStepLoggedFrame != FrameCounter)
    {
        LastStepLoggedFrame = FrameCounter;
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring][Task03] dt=%.6f substeps=%d h=%.6f mode=%s accumulator=%.4f"), InputDelta, CachedSubsteps, CachedH, bUseFixedTimeStep?TEXT("Fixed"):TEXT("Variable"), TimeAccumulator);
    }
#endif
}

void FAnimNode_VRMSpringBone::Evaluate_AnyThread(FPoseContext& Output)
{
    ComponentPose.Evaluate(Output);

    if (!SpringConfig || !SpringConfig->SpringConfig.IsValid() || !bCachesValid)
    {
        return;
    }

    if (CachedSubsteps <= 0)
    {
        return; // nothing scheduled this frame
    }

    const uint64 FrameCounter = GFrameCounter;
    if (LastSimulatedFrame == FrameCounter) return; // already simulated for this frame
    LastSimulatedFrame = FrameCounter;

    // Build a component-space pose from the current evaluated pose so we can fetch live joint/world transforms.
    FCSPose<FCompactPose> CSPose;
    CSPose.InitPose(Output.Pose);
    FAnimationRuntime::FillUpComponentSpaceTransforms(Output.Pose.GetBoneContainer(), Output.Pose, CSPose);

    const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();

    for (FVRMSBChainCache& Chain : ChainCaches)
    {
        if (Chain.Joints.Num() == 0) continue;

        // Gravity (external) term prepared once per chain. (Spec: gravityDir * gravityPower, applied each step scaled by h)
        const FVector ExternalGravity = Chain.GravityDir * Chain.GravityPower;

        // Root->Leaf order already inherent in joint array order (validated during cache build).
        for (int32 j=0; j<Chain.Joints.Num(); ++j)
        {
            FVRMSBJointCache& JC = Chain.Joints[j];
            if (!JC.bValid) continue;
            if (JC.RestLength <= KINDA_SMALL_NUMBER) continue; // leaf-only joint, nothing to simulate yet

            // Current joint (Head) component/world transform
            const FCompactPoseBoneIndex HeadCPIndex = JC.BoneIndex;
            FQuat HeadWorldRot = FQuat::Identity;
            FVector HeadWorldPos = JC.ParentRefPos; // fallback
            if (HeadCPIndex.IsValid())
            {
                const FTransform& HeadCST = CSPose.GetComponentSpaceTransform(HeadCPIndex);
                HeadWorldPos = HeadCST.GetLocation();
                HeadWorldRot = HeadCST.GetRotation();
            }

            // Parent world rotation for stiffness force (spec uses node.parent rotation)
            FQuat ParentWorldRot = FQuat::Identity;
            if (HeadCPIndex.IsValid())
            {
                const FCompactPoseBoneIndex ParentCP = BoneContainer.GetParentBoneIndex(HeadCPIndex);
                if (ParentCP.IsValid())
                {
                    ParentWorldRot = CSPose.GetComponentSpaceTransform(ParentCP).GetRotation();
                }
            }

            // Pre-compute target rest direction in component space = parentWorldRot * (initialLocalRotation * boneAxisLocal)
            FVector RestDirCS = ParentWorldRot.RotateVector(JC.InitialLocalRotation.RotateVector(JC.BoneAxisLocal));
            if (RestDirCS.IsNearlyZero())
            {
                RestDirCS = JC.RestDirection; // fallback to cached rest direction (component space)
            }
            RestDirCS = RestDirCS.GetSafeNormal();

            // Animated target tip position (rest orientation from current joint position)
            const FVector AnimatedRestTip = HeadWorldPos + RestDirCS * JC.RestLength;

            for (int32 Step=0; Step<CachedSubsteps; ++Step)
            {
                // Inertia (Verlet implicit velocity) with drag
                const FVector Velocity = (JC.CurrTip - JC.PrevTip);
                const FVector Inertia = Velocity * (1.0f - Chain.Drag);

                // Stiffness additive force (spec form): h * parentWorldRot * initialLocalRot * boneAxis * stiffness
                const FVector StiffnessForce = RestDirCS * (Chain.Stiffness * CachedH);

                // External gravity force (spec): h * gravityDir * gravityPower
                const FVector GravityForce = ExternalGravity * CachedH;

                FVector NextTip = JC.CurrTip + Inertia + StiffnessForce + GravityForce;

                // Length constraint back to fixed distance from Head
                const FVector ToNext = NextTip - HeadWorldPos;
                const float Dist = ToNext.Size();
                if (Dist > SMALL_NUMBER)
                {
                    NextTip = HeadWorldPos + (ToNext / Dist) * JC.RestLength;
                }
                else
                {
                    NextTip = AnimatedRestTip; // fallback
                }

                JC.PrevTip = JC.CurrTip;
                JC.CurrTip = NextTip;
            }
        }
    }

#if !UE_BUILD_SHIPPING
    if (LastLoggedFrame != FrameCounter)
    {
        LastLoggedFrame = FrameCounter;
        UE_LOG(LogVRMSpring, VeryVerbose, TEXT("[VRMSpring][Tasks04-05] Simulated frame=%llu Springs=%d Joints=%d"), FrameCounter, ChainCaches.Num(), TotalValidJoints);
    }
#endif
}

void FAnimNode_VRMSpringBone::GatherDebugData(FNodeDebugData& DebugData)
{
    DebugData.AddDebugItem(FString::Printf(TEXT("VRMSpringBone (Chains=%d Joints=%d)"), ChainCaches.Num(), TotalValidJoints));
    ComponentPose.GatherDebugData(DebugData);
}

void FAnimNode_VRMSpringBone::InvalidateCaches()
{
    ChainCaches.Reset();
    SphereShapeCaches.Reset();
    CapsuleShapeCaches.Reset();
    LastAssetHash.Reset();
    LastAssetPtr.Reset();
    TotalValidJoints = 0;
    bCachesValid = false;
}

FCompactPoseBoneIndex FAnimNode_VRMSpringBone::ResolveBone(const FBoneContainer& BoneContainer, const FName& BoneName) const
{
    if (BoneName.IsNone())
    {
        return FCompactPoseBoneIndex(INDEX_NONE);
    }
    const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();
    const int32 RefIndex = RefSkeleton.FindBoneIndex(BoneName);
    if (RefIndex == INDEX_NONE)
    {
        return FCompactPoseBoneIndex(INDEX_NONE);
    }
    return BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(RefIndex));
}

FCompactPoseBoneIndex FAnimNode_VRMSpringBone::ResolveBoneByNodeIndex(const FBoneContainer& BoneContainer, int32 NodeIndex) const
{
    if (SpringConfig == nullptr || NodeIndex == INDEX_NONE)
    {
        return FCompactPoseBoneIndex(INDEX_NONE);
    }
    const FName BoneName = SpringConfig->GetBoneNameForNode(NodeIndex);
    return ResolveBone(BoneContainer, BoneName);
}

void FAnimNode_VRMSpringBone::RebuildCaches_AnyThread(const FBoneContainer& BoneContainer)
{
    InvalidateCaches();
    if (!SpringConfig || !SpringConfig->SpringConfig.IsValid()) return;

    const FVRMSpringConfig& Data = SpringConfig->SpringConfig;

    ChainCaches.Reserve(Data.Springs.Num());

    int32 TotalSphereShapes = 0;
    int32 TotalCapsuleShapes = 0;
    for (const FVRMSpringCollider& Collider : Data.Colliders)
    {
        TotalSphereShapes += Collider.Spheres.Num();
        TotalCapsuleShapes += Collider.Capsules.Num();
    }
    SphereShapeCaches.Reserve(TotalSphereShapes);
    CapsuleShapeCaches.Reserve(TotalCapsuleShapes);

    struct FTempColliderMap { TArray<int32> Spheres; TArray<int32> Capsules; };
    TArray<FTempColliderMap> ColliderMaps; ColliderMaps.SetNum(Data.Colliders.Num());

    const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();

    // Colliders
    for (int32 ColliderIdx=0; ColliderIdx < Data.Colliders.Num(); ++ColliderIdx)
    {
        const FVRMSpringCollider& Collider = Data.Colliders[ColliderIdx];
        FCompactPoseBoneIndex BoneIndex = ResolveBone(BoneContainer, Collider.BoneName);
        if (!BoneIndex.IsValid()) BoneIndex = ResolveBoneByNodeIndex(BoneContainer, Collider.NodeIndex);

        FTempColliderMap& Map = ColliderMaps[ColliderIdx];
        Map.Spheres.Reserve(Collider.Spheres.Num());
        Map.Capsules.Reserve(Collider.Capsules.Num());

        const FName BoneName = Collider.BoneName;

        for (const FVRMSpringColliderSphere& Sphere : Collider.Spheres)
        {
            int32 GlobalIdx = SphereShapeCaches.Add(FVRMSBSphereShapeCache{BoneIndex, BoneName, Sphere.Offset, Sphere.Radius, BoneIndex.IsValid() && Sphere.Radius > 0.f});
            Map.Spheres.Add(GlobalIdx);
        }
        for (const FVRMSpringColliderCapsule& Capsule : Collider.Capsules)
        {
            int32 GlobalIdx = CapsuleShapeCaches.Add(FVRMSBCapsuleShapeCache{BoneIndex, BoneName, Capsule.Offset, Capsule.TailOffset, Capsule.Radius, BoneIndex.IsValid() && Capsule.Radius > 0.f});
            Map.Capsules.Add(GlobalIdx);
        }
    }

    TotalValidJoints = 0;
    ChainCaches.SetNum(Data.Springs.Num());

    // Reference pose transforms (component space)
    const TArray<FTransform>& LocalRefPose = RefSkeleton.GetRefBonePose();
    TArray<FTransform> ComponentRefPose; ComponentRefPose.SetNum(RefSkeleton.GetNum());
    for (int32 i=0; i<RefSkeleton.GetNum(); ++i)
    {
        const int32 Parent = RefSkeleton.GetParentIndex(i);
        if (Parent == INDEX_NONE)
        {
            ComponentRefPose[i] = LocalRefPose[i];
        }
        else
        {
            ComponentRefPose[i] = LocalRefPose[i] * ComponentRefPose[Parent];
        }
    }

    // Helper lambda to test ancestry (spec: joints must be ancestor-descendant order)
    auto IsAncestor = [&RefSkeleton](int32 AncestorIdx, int32 DescIdx)->bool
    {
        if (AncestorIdx == INDEX_NONE || DescIdx == INDEX_NONE) return false;
        int32 Walker = DescIdx;
        while (Walker != INDEX_NONE)
        {
            if (Walker == AncestorIdx) return true;
            Walker = RefSkeleton.GetParentIndex(Walker);
        }
        return false;
    };

    for (int32 SpringIdx=0; SpringIdx < Data.Springs.Num(); ++SpringIdx)
    {
        const FVRMSpring& Spring = Data.Springs[SpringIdx];
        FVRMSBChainCache& Chain = ChainCaches[SpringIdx];
        Chain.SpringIndex = SpringIdx;
        Chain.GravityDir = Spring.GravityDir.GetSafeNormal();
        Chain.GravityPower = Spring.GravityPower;
        Chain.Stiffness = FMath::Max(0.f, Spring.Stiffness);
        Chain.Drag = FMath::Clamp(Spring.Drag, 0.f, 1.f);

        // Colliders via groups
        for (int32 GroupIdx : Spring.ColliderGroupIndices)
        {
            if (!Data.ColliderGroups.IsValidIndex(GroupIdx)) continue;
            const FVRMSpringColliderGroup& Group = Data.ColliderGroups[GroupIdx];
            for (int32 ColliderIndex : Group.ColliderIndices)
            {
                if (!ColliderMaps.IsValidIndex(ColliderIndex)) continue;
                const FTempColliderMap& Map = ColliderMaps[ColliderIndex];
                Chain.SphereShapeIndices.Append(Map.Spheres);
                Chain.CapsuleShapeIndices.Append(Map.Capsules);
            }
        }

        Chain.Joints.Reserve(Spring.JointIndices.Num());
        for (int32 JointIdx : Spring.JointIndices)
        {
            if (!Data.Joints.IsValidIndex(JointIdx)) continue;
            const FVRMSpringJoint& Joint = Data.Joints[JointIdx];
            FVRMSBJointCache JC; // default constructed
            FCompactPoseBoneIndex BoneIndex = ResolveBone(BoneContainer, Joint.BoneName);
            if (!BoneIndex.IsValid()) BoneIndex = ResolveBoneByNodeIndex(BoneContainer, Joint.NodeIndex);
            JC.BoneIndex = BoneIndex;
            JC.BoneName = Joint.BoneName;
            JC.HitRadius = Joint.HitRadius > 0.f ? Joint.HitRadius : Spring.HitRadius;
            JC.bValid = BoneIndex.IsValid();
            Chain.Joints.Add(JC);
        }

        // Rest lengths & local axis init
        for (int32 j=0; j < Chain.Joints.Num(); ++j)
        {
            FVRMSBJointCache& JC = Chain.Joints[j];
            if (!JC.bValid) continue;
            const int32 RefIndex = RefSkeleton.FindBoneIndex(JC.BoneName);
            const int32 ParentRefIndex = RefIndex; // parent pointer in this context is this bone; tail is child or virtual end

            FVector ParentPos = FVector::ZeroVector;
            if (ComponentRefPose.IsValidIndex(ParentRefIndex))
            {
                ParentPos = ComponentRefPose[ParentRefIndex].GetLocation();
            }
            JC.ParentRefPos = ParentPos; // reference parent pos

            JC.InitialLocalTransform = LocalRefPose.IsValidIndex(RefIndex) ? LocalRefPose[RefIndex] : FTransform::Identity;
            JC.InitialLocalRotation = JC.InitialLocalTransform.GetRotation();

            if (j+1 < Chain.Joints.Num())
            {
                const FVRMSBJointCache& Child = Chain.Joints[j+1];
                if (Child.bValid)
                {
                    const int32 ChildRefIndex = RefSkeleton.FindBoneIndex(Child.BoneName);
                    if (ComponentRefPose.IsValidIndex(ChildRefIndex))
                    {
                        const FVector ChildPos = ComponentRefPose[ChildRefIndex].GetLocation();
                        const FVector Delta = ChildPos - ParentPos;
                        const float Len = Delta.Length();
                        JC.RestLength = Len;
                        JC.RestDirection = (Len > SMALL_NUMBER) ? (Delta / Len) : FVector::ForwardVector;
                        JC.PrevTip = ChildPos;
                        JC.CurrTip = ChildPos;

                        if (Len > SMALL_NUMBER)
                        {
                            if (ComponentRefPose.IsValidIndex(RefIndex) && ComponentRefPose.IsValidIndex(ChildRefIndex))
                            {
                                const FTransform& JointCS = ComponentRefPose[RefIndex];
                                const FTransform& ChildCS = ComponentRefPose[ChildRefIndex];
                                const FVector ChildVecCS = ChildCS.GetLocation() - JointCS.GetLocation();
                                const FVector LocalDir = JointCS.GetRotation().Inverse().RotateVector(ChildVecCS);
                                JC.BoneAxisLocal = (LocalDir.SizeSquared() > KINDA_SMALL_NUMBER) ? LocalDir.GetSafeNormal() : FVector::ForwardVector;
                                JC.BoneLengthLocal = Len;
                            }
                        }
                    }
                }
            }
            else
            {
                JC.RestLength = 0.f;
                JC.RestDirection = FVector::ForwardVector;
                JC.PrevTip = ParentPos;
                JC.CurrTip = ParentPos;
                JC.BoneAxisLocal = FVector::ForwardVector;
                JC.BoneLengthLocal = 0.f;
            }
        }

        // Simple ancestor validation: ensure each successive valid joint is descendant of previous
        for (int32 j=0; j+1<Chain.Joints.Num(); ++j)
        {
            const FVRMSBJointCache& A = Chain.Joints[j];
            const FVRMSBJointCache& B = Chain.Joints[j+1];
            if (!A.bValid || !B.bValid) continue;
            const int32 AIdx = RefSkeleton.FindBoneIndex(A.BoneName);
            const int32 BIdx = RefSkeleton.FindBoneIndex(B.BoneName);
            if (!(IsAncestor(AIdx, BIdx)))
            {
                UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring][Task02] Joint ordering invalid in spring %d between %s -> %s (not ancestor). This link will be skipped."), SpringIdx, *A.BoneName.ToString(), *B.BoneName.ToString());
                const_cast<FVRMSBJointCache&>(B).bValid = false;
            }
        }

        TotalValidJoints += Chain.Joints.Num();
    }

    LastAssetPtr = SpringConfig;
    LastAssetHash = SpringConfig->GetEffectiveHash();
    bCachesValid = true;
#if !UE_BUILD_SHIPPING
    UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring][Task02] Rebuilt caches: Springs=%d Joints=%d SphereShapes=%d CapsuleShapes=%d"), ChainCaches.Num(), TotalValidJoints, SphereShapeCaches.Num(), CapsuleShapeCaches.Num());
#endif
}

