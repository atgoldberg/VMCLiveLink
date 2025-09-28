#include "AnimNode_VRMSpringBone.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "VRMInterchangeLog.h"

static FORCEINLINE FVector ClosestPointOnSegment(const FVector& A, const FVector& B, const FVector& P)
{
    const FVector AB = B - A;
    const float LenSqr = AB.SizeSquared();
    if (LenSqr <= KINDA_SMALL_NUMBER) return A;
    const float T = FMath::Clamp(FVector::DotProduct(P - A, AB) / LenSqr, 0.f, 1.f);
    return A + AB * T;
}

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
    const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();

    const TArray<FBoneIndexType>& BoneIndices = BoneContainer.GetBoneIndicesArray();
    for (FBoneIndexType BoneIndex : BoneIndices)
    {
        const FCompactPoseBoneIndex CPIndex(BoneIndex);
        const FCompactPoseBoneIndex ParentIndex = BoneContainer.GetParentBoneIndex(CPIndex);
        const FTransform& LocalTransform = Output.Pose[CPIndex]; // local space bone transform
        if (!ParentIndex.IsValid())
        {
            CSPose.SetComponentSpaceTransform(CPIndex, LocalTransform);
        }
        else
        {
            const FTransform& ParentCST = CSPose.GetComponentSpaceTransform(ParentIndex);
            CSPose.SetComponentSpaceTransform(CPIndex, LocalTransform * ParentCST);
        }
    }

    // Task 07: Prepare world-space collider caches (no realloc).
    SphereWorldPos.SetNum(SphereShapeCaches.Num(), /*bAllowShrinking*/false);
    CapsuleWorldP0.SetNum(CapsuleShapeCaches.Num(), false);
    CapsuleWorldP1.SetNum(CapsuleShapeCaches.Num(), false);

    for (int32 i=0; i<SphereShapeCaches.Num(); ++i)
    {
        const FVRMSBSphereShapeCache& SC = SphereShapeCaches[i];
        if (!SC.bValid || !SC.BoneIndex.IsValid())
        {
            SphereWorldPos[i] = FVector::ZeroVector;
            continue;
        }
        const FTransform& BoneCST = CSPose.GetComponentSpaceTransform(SC.BoneIndex);
        SphereWorldPos[i] = BoneCST.TransformPosition(SC.LocalOffset);
    }
    for (int32 i=0; i<CapsuleShapeCaches.Num(); ++i)
    {
        const FVRMSBCapsuleShapeCache& CC = CapsuleShapeCaches[i];
        if (!CC.bValid || !CC.BoneIndex.IsValid())
        {
            CapsuleWorldP0[i] = CapsuleWorldP1[i] = FVector::ZeroVector;
            continue;
        }
        const FTransform& BoneCST = CSPose.GetComponentSpaceTransform(CC.BoneIndex);
        CapsuleWorldP0[i] = BoneCST.TransformPosition(CC.LocalP0);
        CapsuleWorldP1[i] = BoneCST.TransformPosition(CC.LocalP1);
    }

    // Collision resolve helper lambdas.
    auto ResolveSphere = [](const FVector& Center, float Radius, bool bInside, FVector& Tip, float TipRadius)
    {
        const float Combined = Radius + TipRadius;
        const FVector ToTip = Tip - Center;
        const float DistSqr = ToTip.SizeSquared();
        if (DistSqr < KINDA_SMALL_NUMBER)
        {
            const FVector Dir = FVector::UpVector;
            Tip = Center + Dir * (bInside? Combined * 0.99f : Combined + 0.1f);
            return;
        }
        const float Dist = FMath::Sqrt(DistSqr);
        if (!bInside)
        {
            if (Dist < Combined)
            {
                const float Pen = Combined - Dist;
                const FVector N = ToTip / Dist;
                Tip += N * Pen;
            }
        }
        else
        {
            const float Inner = FMath::Max(0.f, Radius - TipRadius);
            if (Dist > Inner)
            {
                const float Pen = Dist - Inner;
                const FVector N = ToTip / Dist;
                Tip -= N * Pen;
            }
        }
    };

    auto ResolveCapsule = [](const FVector& A, const FVector& B, float Radius, bool bInside, FVector& Tip, float TipRadius)
    {
        const float Combined = Radius + TipRadius;
        const FVector Closest = ClosestPointOnSegment(A, B, Tip);
        const FVector ToTip = Tip - Closest;
        const float DistSqr = ToTip.SizeSquared();
        if (DistSqr < KINDA_SMALL_NUMBER)
        {
            FVector Axis = (B - A).GetSafeNormal();
            if (Axis.IsNearlyZero()) Axis = FVector::UpVector;
            FVector Perp = FVector::CrossProduct(Axis, FVector::RightVector);
            if (Perp.IsNearlyZero()) Perp = FVector::UpVector;
            Perp = Perp.GetSafeNormal();
            Tip = Closest + Perp * (bInside? Combined * 0.99f : Combined + 0.1f);
            return;
        }
        const float Dist = FMath::Sqrt(DistSqr);
        if (!bInside)
        {
            if (Dist < Combined)
            {
                const float Pen = Combined - Dist;
                const FVector N = ToTip / Dist;
                Tip += N * Pen;
            }
        }
        else
        {
            const float Inner = FMath::Max(0.f, Radius - TipRadius);
            if (Dist > Inner)
            {
                const float Pen = Dist - Inner;
                const FVector N = ToTip / Dist;
                Tip -= N * Pen;
            }
        }
    };

    for (FVRMSBChainCache& Chain : ChainCaches)
    {
        if (Chain.Joints.Num() == 0) continue;
        const FVector ExternalGravity = Chain.GravityDir * Chain.GravityPower; // per-frame magnitude (authoring intent at 60fps)

        for (int32 Step = 0; Step < CachedSubsteps; ++Step)
        {
            // Frame-rate invariant scaling factor relative to 60Hz authoring assumptions.
            const float s = CachedH * 60.f; // fraction of a 60Hz frame this sub-step represents
            // Precompute scaling factors once per sub-step for this chain.
            const float DragClamped = FMath::Clamp(Chain.Drag, 0.f, 1.f);
            const float StiffClamped = FMath::Clamp(Chain.Stiffness, 0.f, 1.f);
            const float VelKeep = FMath::Pow(FMath::Clamp(1.f - DragClamped, 0.f, 1.f), s); // (1-Drag)^(s)
            const float StiffAlpha = 1.f - FMath::Pow(FMath::Clamp(1.f - StiffClamped, 0.f, 1.f), s); // 1 - (1-Stiffness)^s
            const FVector GravityDisplacement = ExternalGravity * s; // gravity power scaled to sub-step fraction

            for (int32 j = 0; j < Chain.Joints.Num(); ++j)
            {
                FVRMSBJointCache& JC = Chain.Joints[j];
                if (!JC.bValid) continue;
                if (JC.RestLength <= KINDA_SMALL_NUMBER) continue; // leaf placeholder

                const FCompactPoseBoneIndex HeadCPIndex = JC.BoneIndex;
                FQuat HeadWorldRot = FQuat::Identity;
                FVector HeadWorldPos = JC.ParentRefPos; // fallback
                if (HeadCPIndex.IsValid())
                {
                    const FTransform& HeadCST = CSPose.GetComponentSpaceTransform(HeadCPIndex);
                    HeadWorldPos = HeadCST.GetLocation();
                    HeadWorldRot = HeadCST.GetRotation();
                }

                FQuat ParentWorldRot = FQuat::Identity;
                if (HeadCPIndex.IsValid())
                {
                    const FCompactPoseBoneIndex ParentCP = BoneContainer.GetParentBoneIndex(HeadCPIndex);
                    if (ParentCP.IsValid())
                    {
                        ParentWorldRot = CSPose.GetComponentSpaceTransform(ParentCP).GetRotation();
                    }
                }

                FVector RestDirCS = ParentWorldRot.RotateVector(JC.InitialLocalRotation.RotateVector(JC.BoneAxisLocal));
                if (RestDirCS.IsNearlyZero()) RestDirCS = JC.RestDirection;
                RestDirCS = RestDirCS.GetSafeNormal();

                FVector AnchorPos;
                if (j == 0)
                {
                    AnchorPos = HeadWorldPos;
                }
                else
                {
                    const FVRMSBJointCache& PrevJC = Chain.Joints[j-1];
                    if (PrevJC.bValid && PrevJC.RestLength > KINDA_SMALL_NUMBER)
                        AnchorPos = PrevJC.CurrTip;
                    else
                        AnchorPos = HeadWorldPos;
                }

                // Velocity & inertia with frame-rate invariant damping.
                const FVector Velocity = JC.CurrTip - JC.PrevTip;
                FVector NextTip = JC.CurrTip + Velocity * VelKeep; // inertia contribution

                // Gravity displacement (frame-rate invariant).
                NextTip += GravityDisplacement;

                // Positional stiffness blend toward rest target maintaining rest length direction.
                const FVector TargetTip = AnchorPos + RestDirCS * JC.RestLength;
                NextTip = FMath::Lerp(NextTip, TargetTip, StiffAlpha);

                // --- Collisions (sphere then capsule) ---
                const float TipRadius = JC.HitRadius;
                for (int32 SphereIdx : Chain.SphereShapeIndices)
                {
                    if (!SphereShapeCaches.IsValidIndex(SphereIdx)) continue;
                    const FVRMSBSphereShapeCache& SC = SphereShapeCaches[SphereIdx];
                    if (!SC.bValid) continue;
                    ResolveSphere(SphereWorldPos.IsValidIndex(SphereIdx) ? SphereWorldPos[SphereIdx] : FVector::ZeroVector, SC.Radius, SC.bInside, NextTip, TipRadius);
                }
                for (int32 CapsuleIdx : Chain.CapsuleShapeIndices)
                {
                    if (!CapsuleShapeCaches.IsValidIndex(CapsuleIdx)) continue;
                    const FVRMSBCapsuleShapeCache& CC = CapsuleShapeCaches[CapsuleIdx];
                    if (!CC.bValid) continue;
                    ResolveCapsule(CapsuleWorldP0.IsValidIndex(CapsuleIdx)?CapsuleWorldP0[CapsuleIdx]:FVector::ZeroVector,
                                   CapsuleWorldP1.IsValidIndex(CapsuleIdx)?CapsuleWorldP1[CapsuleIdx]:FVector::ZeroVector,
                                   CC.Radius, CC.bInside, NextTip, TipRadius);
                }

                // Final authoritative length clamp after collisions (maintain invariant segment length).
                {
                    const FVector Dir = NextTip - AnchorPos;
                    const float Dist = Dir.Size();
                    if (Dist > SMALL_NUMBER)
                        NextTip = AnchorPos + Dir / Dist * JC.RestLength;
                    else
                        NextTip = AnchorPos + RestDirCS * JC.RestLength;
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
        UE_LOG(LogVRMSpring, VeryVerbose, TEXT("[VRMSpring][Task07+Compat] Simulated frame=%llu Springs=%d Joints=%d (Collisions & VRM param scaling)"), FrameCounter, ChainCaches.Num(), TotalValidJoints);
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
    SphereWorldPos.Reset();
    CapsuleWorldP0.Reset();
    CapsuleWorldP1.Reset();
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
            int32 GlobalIdx = SphereShapeCaches.Add(FVRMSBSphereShapeCache{BoneIndex, BoneName, Sphere.Offset, Sphere.Radius, Sphere.bInside, BoneIndex.IsValid() && Sphere.Radius > 0.f});
            Map.Spheres.Add(GlobalIdx);
        }
        for (const FVRMSpringColliderCapsule& Capsule : Collider.Capsules)
        {
            int32 GlobalIdx = CapsuleShapeCaches.Add(FVRMSBCapsuleShapeCache{BoneIndex, BoneName, Capsule.Offset, Capsule.TailOffset, Capsule.Radius, Capsule.bInside, BoneIndex.IsValid() && Capsule.Radius > 0.f});
            Map.Capsules.Add(GlobalIdx);
        }
    }

    TotalValidJoints = 0;
    ChainCaches.SetNum(Data.Springs.Num());

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

        for (int32 j=0; j < Chain.Joints.Num(); ++j)
        {
            FVRMSBJointCache& JC = Chain.Joints[j];
            if (!JC.bValid) continue;
            const int32 RefIndex = RefSkeleton.FindBoneIndex(JC.BoneName);
            const int32 ParentRefIndex = RefIndex;

            FVector ParentPos = FVector::ZeroVector;
            if (ComponentRefPose.IsValidIndex(ParentRefIndex))
            {
                ParentPos = ComponentRefPose[ParentRefIndex].GetLocation();
            }
            JC.ParentRefPos = ParentPos;

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

