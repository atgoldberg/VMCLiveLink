#include "AnimNode_VRMSpringBone.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "VRMInterchangeLog.h"
#include "Engine/EngineTypes.h"

#if !UE_BUILD_SHIPPING
#include "DrawDebugHelpers.h"
static TAutoConsoleVariable<int32> CVarVRMSpringDraw(
    TEXT("vrm.Spring.Draw"), 0,
    TEXT("VRM SpringBone debug draw: 0=Off 1=Chains 2=Detailed (rest + sim dirs, centers)"),
    ECVF_Default);
static TAutoConsoleVariable<int32> CVarVRMColliderDraw(
    TEXT("vrm.Collider.Draw"), 0,
    TEXT("VRM SpringBone collider debug draw: 0=Off 1=On"),
    ECVF_Default);
#endif

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
    bWasWeightActive = Weight > KINDA_SMALL_NUMBER;
}

void FAnimNode_VRMSpringBone::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    ComponentPose.CacheBones(Context);
    const FBoneContainer& BoneContainer = Context.AnimInstanceProxy->GetRequiredBones();
    bool bNeedsRebuild = !bCachesValid;
    if (SpringConfig == nullptr)
    {
        if (bCachesValid) InvalidateCaches();
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
    if (!SpringConfig || !SpringConfig->SpringConfig.IsValid()) return;
    
    const FString CurrentHash = SpringConfig->GetEffectiveHash();
    if (LastAssetPtr.Get() == SpringConfig && CurrentHash != LastAssetHash)
    {
        const FBoneContainer& BoneContainer = Context.AnimInstanceProxy->GetRequiredBones();
        RebuildCaches_AnyThread(BoneContainer);
    }
    
    if (Weight <= KINDA_SMALL_NUMBER)
    {
        bWasWeightActive = false;
        return;
    }
    
    const float DeltaTime = FMath::Min(Context.GetDeltaTime(), MaxDeltaTime);
    if (DeltaTime <= 0.0f) return;
    
    // Store simulation parameters - we'll do the actual simulation in Evaluate once per frame
    CachedSubsteps = 1; // Match TypeScript - single update per frame
    CachedH = DeltaTime;
    
#if !UE_BUILD_SHIPPING
    const uint64 FrameCounter = GFrameCounter;
    if (LastStepLoggedFrame != FrameCounter)
    {
        LastStepLoggedFrame = FrameCounter;
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring][Update] dt=%.6f mode=SingleStep"), DeltaTime);
    }
#endif
}

void FAnimNode_VRMSpringBone::Evaluate_AnyThread(FPoseContext& Output)
{
    ComponentPose.Evaluate(Output);
    if (!SpringConfig || !SpringConfig->SpringConfig.IsValid() || !bCachesValid) 
    {
#if !UE_BUILD_SHIPPING
        // Still allow debug drawing even if simulation is disabled
        const int32 ColliderDrawMode = CVarVRMColliderDraw.GetValueOnAnyThread();
        if (ColliderDrawMode > 0 && Output.AnimInstanceProxy && bCachesValid)
        {
            FCSPose<FCompactPose> CSPose; 
            CSPose.InitPose(Output.Pose);
            BuildComponentSpacePose(Output, CSPose);
            PrepareColliderWorldCaches(CSPose);
            DrawColliders(Output);
        }
#endif
        return;
    }
    
    const bool bWeightActive = Weight > KINDA_SMALL_NUMBER;
    if (!bWeightActive)
    {
        bWasWeightActive = false;
#if !UE_BUILD_SHIPPING
        const uint64 FrameCounter = GFrameCounter;
        if (LastLoggedFrame != FrameCounter)
        {
            LastLoggedFrame = FrameCounter;
            UE_LOG(LogVRMSpring, VeryVerbose, TEXT("[VRMSpring][Evaluate] Skipped (Weight=0) frame=%llu"), FrameCounter);
        }
        
        // Still allow debug drawing even when weight is 0
        const int32 SpringDrawMode = CVarVRMSpringDraw.GetValueOnAnyThread();
        const int32 ColliderDrawMode = CVarVRMColliderDraw.GetValueOnAnyThread();
        if ((SpringDrawMode > 0 || ColliderDrawMode > 0) && Output.AnimInstanceProxy)
        {
            FCSPose<FCompactPose> CSPose; 
            CSPose.InitPose(Output.Pose);
            BuildComponentSpacePose(Output, CSPose);
            PrepareColliderWorldCaches(CSPose);
            
            if (SpringDrawMode > 0)
            {
                DrawSpringBones(Output, CSPose, SpringDrawMode);
            }
            if (ColliderDrawMode > 0)
            {
                DrawColliders(Output);
            }
        }
#endif
        return;
    }
    
    // Only simulate once per frame - matching TypeScript behavior
    const uint64 FrameCounter = GFrameCounter;
    if (LastSimulatedFrame == FrameCounter) 
    {
#if !UE_BUILD_SHIPPING
        // Even if we already simulated this frame, still draw debug info
        const int32 SpringDrawMode = CVarVRMSpringDraw.GetValueOnAnyThread();
        const int32 ColliderDrawMode = CVarVRMColliderDraw.GetValueOnAnyThread();
        if ((SpringDrawMode > 0 || ColliderDrawMode > 0) && Output.AnimInstanceProxy)
        {
            FCSPose<FCompactPose> CSPose; 
            CSPose.InitPose(Output.Pose);
            BuildComponentSpacePose(Output, CSPose);
            PrepareColliderWorldCaches(CSPose);
            
            if (SpringDrawMode > 0)
            {
                DrawSpringBones(Output, CSPose, SpringDrawMode);
            }
            if (ColliderDrawMode > 0)
            {
                DrawColliders(Output);
            }
        }
#endif
        return;
    }
    LastSimulatedFrame = FrameCounter;
    
    if (CachedH <= 0.0f) { bWasWeightActive = bWeightActive; return; }
    
    FCSPose<FCompactPose> CSPose; 
    CSPose.InitPose(Output.Pose);
    const FBoneContainer& BoneContainer = Output.Pose.GetBoneContainer();
    BuildComponentSpacePose(Output, CSPose);
    
    // Initialize spring state when transitioning from inactive to active
    if (!bWasWeightActive && bWeightActive)
    {
        InitializeSpringState(CSPose);
    }
    bWasWeightActive = bWeightActive;
    
    PrepareColliderWorldCaches(CSPose);
    SimulateChains(BoneContainer, CSPose);
    ApplyRotations(Output, CSPose);
    
#if !UE_BUILD_SHIPPING
    if (LastLoggedFrame != FrameCounter)
    {
        LastLoggedFrame = FrameCounter;
        UE_LOG(LogVRMSpring, VeryVerbose, TEXT("[VRMSpring][Evaluate] Sim frame=%llu Springs=%d Joints=%d Weight=%.3f"), FrameCounter, ChainCaches.Num(), TotalValidJoints, Weight);
    }
    
    // Consolidated debug drawing
    const int32 SpringDrawMode = CVarVRMSpringDraw.GetValueOnAnyThread();
    const int32 ColliderDrawMode = CVarVRMColliderDraw.GetValueOnAnyThread();
    if ((SpringDrawMode > 0 || ColliderDrawMode > 0) && Output.AnimInstanceProxy)
    {
        if (SpringDrawMode > 0)
        {
            DrawSpringBones(Output, CSPose, SpringDrawMode);
        }
        if (ColliderDrawMode > 0)
        {
            DrawColliders(Output);
        }
    }
#endif
}

void FAnimNode_VRMSpringBone::GatherDebugData(FNodeDebugData& DebugData)
{
    DebugData.AddDebugItem(FString::Printf(TEXT("VRMSpringBone (Chains=%d Joints=%d W=%.2f)"), ChainCaches.Num(), TotalValidJoints, Weight));
    ComponentPose.GatherDebugData(DebugData);
}

void FAnimNode_VRMSpringBone::BuildComponentSpacePose(const FPoseContext& SourcePose, FCSPose<FCompactPose>& OutCSPose) const
{
    const FBoneContainer& BoneContainer = SourcePose.Pose.GetBoneContainer();
    const TArray<FBoneIndexType>& BoneIndices = BoneContainer.GetBoneIndicesArray();
    for (FBoneIndexType BoneIndex : BoneIndices)
    {
        const FCompactPoseBoneIndex CPIndex(BoneIndex);
        const FCompactPoseBoneIndex ParentIndex = BoneContainer.GetParentBoneIndex(CPIndex);
        const FTransform& LocalTransform = SourcePose.Pose[CPIndex];
        if (!ParentIndex.IsValid())
        {
            OutCSPose.SetComponentSpaceTransform(CPIndex, LocalTransform);
        }
        else
        {
            const FTransform& ParentCST = OutCSPose.GetComponentSpaceTransform(ParentIndex);
            OutCSPose.SetComponentSpaceTransform(CPIndex, LocalTransform * ParentCST);
        }
    }
}

void FAnimNode_VRMSpringBone::PrepareColliderWorldCaches(FCSPose<FCompactPose>& CSPose)
{
    SphereWorldPos.SetNum(SphereShapeCaches.Num(), EAllowShrinking::No);
    CapsuleWorldP0.SetNum(CapsuleShapeCaches.Num(), EAllowShrinking::No);
    CapsuleWorldP1.SetNum(CapsuleShapeCaches.Num(), EAllowShrinking::No);
    PlaneWorldPoint.SetNum(PlaneShapeCaches.Num(), EAllowShrinking::No);
    PlaneWorldNormal.SetNum(PlaneShapeCaches.Num(), EAllowShrinking::No);
    for (int32 i=0; i<SphereShapeCaches.Num(); ++i)
    {
        const FVRMSBSphereShapeCache& SC = SphereShapeCaches[i];
        if (!SC.bValid || !SC.BoneIndex.IsValid()) { SphereWorldPos[i] = FVector::ZeroVector; continue; }
        const FTransform& BoneCST = CSPose.GetComponentSpaceTransform(SC.BoneIndex);
        SphereWorldPos[i] = BoneCST.TransformPosition(SC.LocalOffset);
    }
    for (int32 i=0; i<CapsuleShapeCaches.Num(); ++i)
    {
        const FVRMSBCapsuleShapeCache& CC = CapsuleShapeCaches[i];
        if (!CC.bValid || !CC.BoneIndex.IsValid()) { CapsuleWorldP0[i] = CapsuleWorldP1[i] = FVector::ZeroVector; continue; }
        const FTransform& BoneCST = CSPose.GetComponentSpaceTransform(CC.BoneIndex);
        CapsuleWorldP0[i] = BoneCST.TransformPosition(CC.LocalP0);
        CapsuleWorldP1[i] = BoneCST.TransformPosition(CC.LocalP1);
    }
    for (int32 i=0; i<PlaneShapeCaches.Num(); ++i)
    {
        const FVRMSBPlaneShapeCache& PC = PlaneShapeCaches[i];
        if (!PC.bValid || !PC.BoneIndex.IsValid()) { PlaneWorldPoint[i] = FVector::ZeroVector; PlaneWorldNormal[i] = FVector::UpVector; continue; }
        const FTransform& BoneCST = CSPose.GetComponentSpaceTransform(PC.BoneIndex);
        PlaneWorldPoint[i]  = BoneCST.TransformPosition(PC.LocalPoint);
        PlaneWorldNormal[i] = BoneCST.TransformVectorNoScale(PC.LocalNormal).GetSafeNormal();
        if (PlaneWorldNormal[i].IsNearlyZero()) PlaneWorldNormal[i] = FVector::UpVector;
    }
}

void FAnimNode_VRMSpringBone::InitializeSpringState(FCSPose<FCompactPose>& CSPose)
{
    for (FVRMSBChainCache& Chain : ChainCaches)
    {
        for (int32 j = 0; j < Chain.Joints.Num(); ++j)
        {
            FVRMSBJointCache& JC = Chain.Joints[j];
            if (!JC.bValid || JC.RestLength <= KINDA_SMALL_NUMBER) continue;
            
            // Initialize tail position based on current bone state
            FVector ChildPos = JC.CurrentTail;
            if (j + 1 < Chain.Joints.Num())
            {
                const FVRMSBJointCache& Child = Chain.Joints[j + 1];
                if (Child.bValid && Child.BoneIndex.IsValid())
                {
                    ChildPos = CSPose.GetComponentSpaceTransform(Child.BoneIndex).GetLocation();
                }
            }
            else if (JC.BoneIndex.IsValid())
            {
                // For leaf joints, use bone position + rest direction * rest length
                const FTransform& ParentCST = CSPose.GetComponentSpaceTransform(JC.BoneIndex);
                const FVector ParentPos = ParentCST.GetLocation();
                const FQuat ParentRot = ParentCST.GetRotation();
                ChildPos = ParentPos + ParentRot.RotateVector(JC.BoneAxis) * JC.RestLength;
            }
            
            JC.CurrentTail = ChildPos;
            JC.PrevTail = ChildPos;
        }
    }
}

void FAnimNode_VRMSpringBone::SimulateChains(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& CSPose)
{
    auto ResolveSphere = [](const FVector& Center, float Radius, bool bInside, FVector& Tip, float TipRadius) {
        const float Combined = Radius + TipRadius;
        const FVector ToTip = Tip - Center;
        const float DistSqr = ToTip.SizeSquared();
        
        if (DistSqr < KINDA_SMALL_NUMBER) {
            const FVector Dir = FVector::UpVector;
            Tip = Center + Dir * (bInside ? Combined * 0.99f : Combined + 0.1f);
            return;
        }
        
        const float Dist = FMath::Sqrt(DistSqr);
        if (!bInside) {
            if (Dist < Combined) {
                const float Pen = Combined - Dist;
                const FVector N = ToTip / Dist;
                Tip += N * Pen;
            }
        } else {
            const float Inner = FMath::Max(0.f, Radius - TipRadius);
            if (Dist > Inner) {
                const float Pen = Dist - Inner;
                const FVector N = ToTip / Dist;
                Tip -= N * Pen;
            }
        }
    };

    auto ResolveCapsule = [](const FVector& A, const FVector& B, float Radius, bool bInside, FVector& Tip, float TipRadius) {
        const float Combined = Radius + TipRadius;
        const FVector OffsetToTail = B - A;
        const FVector Delta = Tip - A;
        const float Dot = FVector::DotProduct(OffsetToTail, Delta);
        
        FVector AdjustedDelta = Delta;
        const float OffsetToTailLenSqr = OffsetToTail.SizeSquared();
        
        if (Dot < 0.0f) {
            // Head side - use delta as-is
        } else if (Dot > OffsetToTailLenSqr) {
            // Tail side
            AdjustedDelta = Delta - OffsetToTail;
        } else {
            // Between head and tail
            if (OffsetToTailLenSqr > KINDA_SMALL_NUMBER) {
                AdjustedDelta = Delta - OffsetToTail * (Dot / OffsetToTailLenSqr);
            }
        }
        
        const float DistSqr = AdjustedDelta.SizeSquared();
        if (DistSqr < KINDA_SMALL_NUMBER) {
            FVector Axis = OffsetToTail.GetSafeNormal();
            if (Axis.IsNearlyZero()) Axis = FVector::UpVector;
            FVector Perp = FVector::CrossProduct(Axis, FVector::RightVector);
            if (Perp.IsNearlyZero()) Perp = FVector::CrossProduct(Axis, FVector::ForwardVector);
            Perp.Normalize();
            Tip = A + Perp * (bInside ? Combined * 0.99f : Combined + 0.1f);
            return;
        }
        
        const float Dist = FMath::Sqrt(DistSqr);
        const FVector Direction = AdjustedDelta / Dist;
        
        if (!bInside) {
            if (Dist < Combined) {
                const float Penetration = Combined - Dist;
                Tip += Direction * Penetration;
            }
        } else {
            const float Inner = FMath::Max(0.f, Radius - TipRadius);
            if (Dist > Inner) {
                const float Penetration = Dist - Inner;
                Tip -= Direction * Penetration;
            }
        }
    };

    auto ResolvePlane = [](const FVector& P0, const FVector& N, FVector& Tip, float TipRadius) {
        const float Dist = FVector::DotProduct(Tip - P0, N) - TipRadius;
        if (Dist < 0.f) {
            Tip -= N * Dist;
        }
    };

    for (FVRMSBChainCache& Chain : ChainCaches)
    {
        if (Chain.Joints.Num() == 0) continue;
        
        // Get center transform for coordinate space conversion
        FTransform CenterCST = FTransform::Identity;
        FTransform CenterInv = FTransform::Identity;
        if (Chain.bHasCenter && Chain.CenterBoneIndex.IsValid()) {
            CenterCST = CSPose.GetComponentSpaceTransform(Chain.CenterBoneIndex);
            CenterInv = CenterCST.Inverse();
        }
        
        // Single simulation step per frame - matching TypeScript
        const float DeltaTime = CachedH;
        
        for (int32 j = 0; j < Chain.Joints.Num(); ++j)
        {
            FVRMSBJointCache& JC = Chain.Joints[j];
            if (!JC.bValid || JC.RestLength <= KINDA_SMALL_NUMBER) continue;
            
            const FCompactPoseBoneIndex HeadCPIndex = JC.BoneIndex;
            FVector HeadWorldPos = JC.ParentRefPos;
            
            if (HeadCPIndex.IsValid()) {
                const FTransform& HeadCST = CSPose.GetComponentSpaceTransform(HeadCPIndex);
                HeadWorldPos = HeadCST.GetLocation();
            }
            
            // Get anchor position (parent joint or bone position)
            const FVector AnchorPos = (j == 0) ? HeadWorldPos : 
                (Chain.Joints[j-1].bValid && Chain.Joints[j-1].RestLength > KINDA_SMALL_NUMBER ? 
                 Chain.Joints[j-1].CurrentTail : HeadWorldPos);
            
            // Convert to center space if needed
            FVector AnchorSim = Chain.bHasCenter ? CenterInv.TransformPosition(AnchorPos) : AnchorPos;
            FVector CurrentSim = Chain.bHasCenter ? CenterInv.TransformPosition(JC.CurrentTail) : JC.CurrentTail;
            FVector PrevSim = Chain.bHasCenter ? CenterInv.TransformPosition(JC.PrevTail) : JC.PrevTail;
            
            // Verlet integration - matching TypeScript implementation
            FVector Velocity = (CurrentSim - PrevSim) * (1.0f - Chain.Drag);
            
            // Calculate rest direction in current space
            FQuat ParentWorldRot = FQuat::Identity;
            if (HeadCPIndex.IsValid()) {
                const FCompactPoseBoneIndex ParentCP = BoneContainer.GetParentBoneIndex(HeadCPIndex);
                if (ParentCP.IsValid()) {
                    ParentWorldRot = CSPose.GetComponentSpaceTransform(ParentCP).GetRotation();
                }
            }
            
            FVector RestDirCS = ParentWorldRot.RotateVector(JC.InitialLocalRotation.RotateVector(JC.BoneAxis)).GetSafeNormal();
            if (RestDirCS.IsNearlyZero()) RestDirCS = JC.RestDirection;
            
            const FVector RestDirSim = Chain.bHasCenter ? 
                CenterCST.GetRotation().Inverse().RotateVector(RestDirCS) : RestDirCS;
            
            // Target position based on stiffness
            const FVector RestTarget = AnchorSim + RestDirSim * JC.RestLength;
            
            // Apply forces - matching TypeScript approach
            FVector NextSim = CurrentSim + Velocity;
            
            // Apply stiffness toward rest target - matching TypeScript implementation
            NextSim += (RestTarget - CurrentSim) * Chain.Stiffness * DeltaTime;
            
            // Apply gravity in simulation space
            FVector GravityDisp = Chain.GravityDir * Chain.GravityPower * DeltaTime;
            if (Chain.bHasCenter) {
                GravityDisp = CenterCST.GetRotation().Inverse().RotateVector(GravityDisp);
            }
            NextSim += GravityDisp;
            
            // Convert back to world space for collision testing
            FVector NextCS = Chain.bHasCenter ? CenterCST.TransformPosition(NextSim) : NextSim;
            FVector AnchorCS = Chain.bHasCenter ? CenterCST.TransformPosition(AnchorSim) : AnchorPos;
            
            // Collision resolution
            const float TipRadius = JC.HitRadius;
            for (int32 SphereIdx : Chain.SphereShapeIndices) {
                if (!SphereShapeCaches.IsValidIndex(SphereIdx)) continue;
                const FVRMSBSphereShapeCache& SC = SphereShapeCaches[SphereIdx];
                if (!SC.bValid) continue;
                FVector Tmp = NextCS;
                ResolveSphere(SphereWorldPos.IsValidIndex(SphereIdx) ? SphereWorldPos[SphereIdx] : FVector::ZeroVector, 
                             SC.Radius, SC.bInside, Tmp, TipRadius);
                NextCS = Tmp;
            }
            
            for (int32 CapsuleIdx : Chain.CapsuleShapeIndices) {
                if (!CapsuleShapeCaches.IsValidIndex(CapsuleIdx)) continue;
                const FVRMSBCapsuleShapeCache& CC = CapsuleShapeCaches[CapsuleIdx];
                if (!CC.bValid) continue;
                FVector Tmp = NextCS;
                ResolveCapsule(CapsuleWorldP0.IsValidIndex(CapsuleIdx) ? CapsuleWorldP0[CapsuleIdx] : FVector::ZeroVector,
                              CapsuleWorldP1.IsValidIndex(CapsuleIdx) ? CapsuleWorldP1[CapsuleIdx] : FVector::ZeroVector,
                              CC.Radius, CC.bInside, Tmp, TipRadius);
                NextCS = Tmp;
            }
            
            for (int32 PlaneIdx : Chain.PlaneShapeIndices) {
                if (!PlaneShapeCaches.IsValidIndex(PlaneIdx)) continue;
                const FVRMSBPlaneShapeCache& PC = PlaneShapeCaches[PlaneIdx];
                if (!PC.bValid) continue;
                FVector Tmp = NextCS;
                ResolvePlane(PlaneWorldPoint.IsValidIndex(PlaneIdx) ? PlaneWorldPoint[PlaneIdx] : FVector::ZeroVector,
                            PlaneWorldNormal.IsValidIndex(PlaneIdx) ? PlaneWorldNormal[PlaneIdx] : FVector::UpVector,
                            Tmp, TipRadius);
                NextCS = Tmp;
            }
            
            // Constraint to maintain bone length - matching TypeScript normalization
            const FVector DirCS = NextCS - AnchorCS;
            const float Dist = DirCS.Size();
            if (Dist > SMALL_NUMBER) {
                NextCS = AnchorCS + (DirCS / Dist) * JC.RestLength;
            } else {
                NextCS = AnchorCS + RestDirCS * JC.RestLength;
            }
            
            // Validate result
            auto IsVecFinite = [](const FVector& V) { 
                return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); 
            };
            if (!IsVecFinite(NextCS) || NextCS.ContainsNaN()) {
                NextCS = AnchorPos + RestDirCS * JC.RestLength;
            }
            
            // Update spring state - matching TypeScript variable names
            JC.PrevTail = JC.CurrentTail;
            JC.CurrentTail = NextCS;
        }
    }
}

void FAnimNode_VRMSpringBone::ApplyRotations(FPoseContext& Output, FCSPose<FCompactPose>& CSPose)
{
    const float kRotationDeadZoneRad = FMath::DegreesToRadians(FMath::Max(0.f, RotationDeadZoneDeg));
    for (FVRMSBChainCache& Chain : ChainCaches)
    {
        if (Chain.Joints.Num() < 2) continue;
        for (int32 j=0; j<Chain.Joints.Num(); ++j)
        {
            FVRMSBJointCache& JC = Chain.Joints[j]; 
            if (!JC.bValid || JC.RestLength <= KINDA_SMALL_NUMBER) continue;
            const FCompactPoseBoneIndex ParentCP = JC.BoneIndex; 
            if (!ParentCP.IsValid()) continue;
            const FTransform& ParentCST = CSPose.GetComponentSpaceTransform(ParentCP);
            const FVector ParentWorldPos = ParentCST.GetLocation(); 
            const FVector SimTipPos = JC.CurrentTail; 
            FVector SimDirCS = SimTipPos - ParentWorldPos; 
            if (!SimDirCS.Normalize()) continue;
            const FQuat ParentWorldRot = ParentCST.GetRotation(); 
            FVector SimDirLocal = ParentWorldRot.Inverse().RotateVector(SimDirCS); 
            if (!SimDirLocal.Normalize()) continue;
            const FQuat FromTo = FQuat::FindBetweenNormals(JC.BoneAxis, SimDirLocal); 
            if (!FromTo.IsNormalized()) continue; 
            if (FromTo.GetAngle() < kRotationDeadZoneRad) continue;
            const FQuat SpecRot = (JC.InitialLocalRotation * FromTo).GetNormalized();
            FTransform& LocalTransform = Output.Pose[ParentCP];
            if (Weight >= 1.f - KINDA_SMALL_NUMBER) 
                LocalTransform.SetRotation(SpecRot); 
            else 
                LocalTransform.SetRotation(FQuat::Slerp(LocalTransform.GetRotation(), SpecRot, Weight).GetNormalized());
        }
    }
}

void FAnimNode_VRMSpringBone::InvalidateCaches()
{
    ChainCaches.Reset(); SphereShapeCaches.Reset(); CapsuleShapeCaches.Reset(); PlaneShapeCaches.Reset();
    LastAssetHash.Reset(); LastAssetPtr.Reset(); TotalValidJoints = 0; bCachesValid = false;
    SphereWorldPos.Reset(); CapsuleWorldP0.Reset(); CapsuleWorldP1.Reset(); PlaneWorldPoint.Reset(); PlaneWorldNormal.Reset();
}

FCompactPoseBoneIndex FAnimNode_VRMSpringBone::ResolveBone(const FBoneContainer& BoneContainer, const FName& BoneName) const
{
    if (BoneName.IsNone()) return FCompactPoseBoneIndex(INDEX_NONE);
    const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();
    const int32 RefIndex = RefSkeleton.FindBoneIndex(BoneName);
    if (RefIndex == INDEX_NONE) return FCompactPoseBoneIndex(INDEX_NONE);
    return BoneContainer.MakeCompactPoseIndex(FMeshPoseBoneIndex(RefIndex));
}

FCompactPoseBoneIndex FAnimNode_VRMSpringBone::ResolveBoneByNodeIndex(const FBoneContainer& BoneContainer, int32 NodeIndex) const
{
    if (SpringConfig == nullptr || NodeIndex == INDEX_NONE) {
        return FCompactPoseBoneIndex(INDEX_NONE);
    }

    // Primary: use node->bone map from data asset
    const FName BoneName = SpringConfig->GetBoneNameForNode(NodeIndex);
    if (!BoneName.IsNone()) {
        return ResolveBone(BoneContainer, BoneName);
    }

    // Fallback: interpret node index directly as reference skeleton index (legacy behavior)
    const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();
    if (NodeIndex >= 0 && NodeIndex < RefSkeleton.GetNum()) {
        const FName FallbackName = RefSkeleton.GetBoneName(NodeIndex);
        UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring] Using fallback bone resolution for node %d -> %s"), NodeIndex, *FallbackName.ToString());
        return ResolveBone(BoneContainer, FallbackName);
    }

    return FCompactPoseBoneIndex(INDEX_NONE);
}

void FAnimNode_VRMSpringBone::RebuildCaches_AnyThread(const FBoneContainer& BoneContainer)
{
    InvalidateCaches();
    if (!SpringConfig || !SpringConfig->SpringConfig.IsValid()) return;
    const FVRMSpringConfig& Data = SpringConfig->SpringConfig;
    ChainCaches.Reserve(Data.Springs.Num());
    int32 TotalSphereShapes=0, TotalCapsuleShapes=0, TotalPlaneShapes=0;
    for (const FVRMSpringCollider& Collider : Data.Colliders){ TotalSphereShapes += Collider.Spheres.Num(); TotalCapsuleShapes += Collider.Capsules.Num(); TotalPlaneShapes += Collider.Planes.Num(); }
    SphereShapeCaches.Reserve(TotalSphereShapes); CapsuleShapeCaches.Reserve(TotalCapsuleShapes); PlaneShapeCaches.Reserve(TotalPlaneShapes);
    struct FTempColliderMap { TArray<int32> Spheres; TArray<int32> Capsules; TArray<int32> Planes; };
    TArray<FTempColliderMap> ColliderMaps; ColliderMaps.SetNum(Data.Colliders.Num());
    const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();
    for (int32 ColliderIdx=0; ColliderIdx < Data.Colliders.Num(); ++ColliderIdx)
    {
        const FVRMSpringCollider& Collider = Data.Colliders[ColliderIdx];
        FCompactPoseBoneIndex BoneIndex = ResolveBone(BoneContainer, Collider.BoneName);
        if (!BoneIndex.IsValid()) BoneIndex = ResolveBoneByNodeIndex(BoneContainer, Collider.NodeIndex);
        FTempColliderMap& Map = ColliderMaps[ColliderIdx]; Map.Spheres.Reserve(Collider.Spheres.Num()); Map.Capsules.Reserve(Collider.Capsules.Num()); Map.Planes.Reserve(Collider.Planes.Num());
        const FName BoneName = Collider.BoneName;
        for (const FVRMSpringColliderSphere& Sphere : Collider.Spheres){ int32 GlobalIdx = SphereShapeCaches.Add(FVRMSBSphereShapeCache{BoneIndex,BoneName,Sphere.Offset,Sphere.Radius,Sphere.bInside,BoneIndex.IsValid() && Sphere.Radius>0.f}); Map.Spheres.Add(GlobalIdx); }
        for (const FVRMSpringColliderCapsule& Capsule : Collider.Capsules){ int32 GlobalIdx = CapsuleShapeCaches.Add(FVRMSBCapsuleShapeCache{BoneIndex,BoneName,Capsule.Offset,Capsule.TailOffset,Capsule.Radius,Capsule.bInside,BoneIndex.IsValid() && Capsule.Radius>0.f}); Map.Capsules.Add(GlobalIdx); }
        for (const FVRMSpringColliderPlane& Plane : Collider.Planes){ const FVector Nrm = Plane.Normal.GetSafeNormal(); int32 GlobalIdx = PlaneShapeCaches.Add(FVRMSBPlaneShapeCache{BoneIndex,BoneName,Plane.Offset,(Nrm.IsNearlyZero()?FVector(0,0,1):Nrm), BoneIndex.IsValid()}); Map.Planes.Add(GlobalIdx); }
    }
    TotalValidJoints = 0; ChainCaches.SetNum(Data.Springs.Num());
    const TArray<FTransform>& LocalRefPose = RefSkeleton.GetRefBonePose();
    TArray<FTransform> ComponentRefPose; ComponentRefPose.SetNum(RefSkeleton.GetNum());
    for (int32 i=0;i<RefSkeleton.GetNum();++i){ const int32 Parent = RefSkeleton.GetParentIndex(i); ComponentRefPose[i] = (Parent==INDEX_NONE)?LocalRefPose[i]:LocalRefPose[i]*ComponentRefPose[Parent]; }
    auto IsAncestor = [&RefSkeleton](int32 AncestorIdx,int32 DescIdx){ int32 W=DescIdx; while (W!=INDEX_NONE){ if (W==AncestorIdx) return true; W=RefSkeleton.GetParentIndex(W);} return false; };
    for (int32 SpringIdx=0; SpringIdx < Data.Springs.Num(); ++SpringIdx)
    {
        const FVRMSpring& Spring = Data.Springs[SpringIdx]; FVRMSBChainCache& Chain = ChainCaches[SpringIdx];
        Chain.SpringIndex = SpringIdx; Chain.GravityDir = Spring.GravityDir.GetSafeNormal(); Chain.GravityPower = Spring.GravityPower; Chain.Stiffness = FMath::Max(0.f, Spring.Stiffness); Chain.Drag = FMath::Clamp(Spring.Drag,0.f,1.f);
        Chain.CenterBoneIndex = ResolveBone(BoneContainer, Spring.CenterBoneName); if (!Chain.CenterBoneIndex.IsValid()) Chain.CenterBoneIndex = ResolveBoneByNodeIndex(BoneContainer, Spring.CenterNodeIndex); Chain.CenterBoneName = Spring.CenterBoneName; Chain.bHasCenter = Chain.CenterBoneIndex.IsValid();
        TArray<int32> SphereUnique, CapsuleUnique, PlaneUnique; for (int32 GroupIdx : Spring.ColliderGroupIndices){ if (!Data.ColliderGroups.IsValidIndex(GroupIdx)) continue; const FVRMSpringColliderGroup& Group = Data.ColliderGroups[GroupIdx]; for (int32 ColliderIndex : Group.ColliderIndices){ if (!ColliderMaps.IsValidIndex(ColliderIndex)) continue; const FTempColliderMap& Map = ColliderMaps[ColliderIndex]; for (int32 S:Map.Spheres) SphereUnique.AddUnique(S); for (int32 C:Map.Capsules) CapsuleUnique.AddUnique(C); for (int32 P:Map.Planes) PlaneUnique.AddUnique(P);} }
        Chain.SphereShapeIndices = SphereUnique; Chain.CapsuleShapeIndices = CapsuleUnique; Chain.PlaneShapeIndices = PlaneUnique;
        Chain.Joints.Reserve(Spring.JointIndices.Num());
        for (int32 JointIdx : Spring.JointIndices){ if (!Data.Joints.IsValidIndex(JointIdx)) continue; const FVRMSpringJoint& Joint = Data.Joints[JointIdx]; FVRMSBJointCache JC; FCompactPoseBoneIndex BoneIndex = ResolveBone(BoneContainer, Joint.BoneName); if (!BoneIndex.IsValid()) BoneIndex = ResolveBoneByNodeIndex(BoneContainer, Joint.NodeIndex); JC.BoneIndex=BoneIndex; JC.BoneName=Joint.BoneName; JC.HitRadius = Joint.HitRadius>0.f?Joint.HitRadius:Spring.HitRadius; JC.bValid = BoneIndex.IsValid(); Chain.Joints.Add(JC);}        
        for (int32 j=0;j<Chain.Joints.Num();++j)
        { 
            FVRMSBJointCache& JC = Chain.Joints[j]; 
            if (!JC.bValid) continue; 
            const int32 RefIndex = RefSkeleton.FindBoneIndex(JC.BoneName); 
            if (RefIndex==INDEX_NONE){ JC.bValid=false; continue;} 
            FVector JointRefPos = ComponentRefPose.IsValidIndex(RefIndex)?ComponentRefPose[RefIndex].GetLocation():FVector::ZeroVector; 
            JC.ParentRefPos=JointRefPos; 
            JC.InitialLocalTransform = LocalRefPose.IsValidIndex(RefIndex)?LocalRefPose[RefIndex]:FTransform::Identity; 
            JC.InitialLocalRotation = JC.InitialLocalTransform.GetRotation(); 
            if (j+1<Chain.Joints.Num())
            { 
                FVRMSBJointCache& Child=Chain.Joints[j+1]; 
                if (Child.bValid)
                { 
                    const int32 ChildRefIndex = RefSkeleton.FindBoneIndex(Child.BoneName); 
                    if (ComponentRefPose.IsValidIndex(ChildRefIndex))
                    { 
                        const FVector ChildPos = ComponentRefPose[ChildRefIndex].GetLocation(); 
                        const FVector Delta = ChildPos - JointRefPos; 
                        const float Len = Delta.Size(); 
                        JC.RestLength = Len; 
                        JC.RestDirection = (Len>SMALL_NUMBER)?(Delta/Len):FVector::ForwardVector; 
                        JC.PrevTail = ChildPos; 
                        JC.CurrentTail = ChildPos; 
                        if (Len>SMALL_NUMBER)
                        { 
                            const FTransform& JointCS = ComponentRefPose[RefIndex]; 
                            const FTransform& ChildCS = ComponentRefPose[ChildRefIndex]; 
                            const FVector ChildVecCS = ChildCS.GetLocation()-JointCS.GetLocation(); 
                            const FVector LocalDir = JointCS.GetRotation().Inverse().RotateVector(ChildVecCS); 
                            JC.BoneAxis = (LocalDir.SizeSquared()>KINDA_SMALL_NUMBER)?LocalDir.GetSafeNormal():FVector::ForwardVector; 
                            JC.WorldSpaceBoneLength = Len; 
                        } 
                    } 
                } 
            } 
            else 
            { 
                JC.RestLength=0.f; 
                JC.RestDirection=FVector::ForwardVector; 
                JC.PrevTail=JointRefPos; 
                JC.CurrentTail=JointRefPos; 
                JC.BoneAxis=FVector::ForwardVector; 
                JC.WorldSpaceBoneLength=0.f; 
            } 
        }
        for (int32 j=0;j+1<Chain.Joints.Num();++j){ FVRMSBJointCache& A=Chain.Joints[j]; FVRMSBJointCache& B=Chain.Joints[j+1]; if (!A.bValid||!B.bValid) continue; const int32 AIdx=RefSkeleton.FindBoneIndex(A.BoneName); const int32 BIdx=RefSkeleton.FindBoneIndex(B.BoneName); if (!IsAncestor(AIdx,BIdx)){ UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring][Cache] Joint ordering invalid in spring %d between %s -> %s (not ancestor). Skipping child."), SpringIdx, *A.BoneName.ToString(), *B.BoneName.ToString()); B.bValid=false; }}
        for (const FVRMSBJointCache& JC : Chain.Joints){ if (JC.bValid && JC.RestLength > KINDA_SMALL_NUMBER) ++TotalValidJoints; }
    }
    LastAssetPtr = SpringConfig; LastAssetHash = SpringConfig->GetEffectiveHash(); bCachesValid = true;
#if !UE_BUILD_SHIPPING
    UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring][Cache] Rebuilt caches: Springs=%d Joints=%d SphereShapes=%d CapsuleShapes=%d PlaneShapes=%d"), ChainCaches.Num(), TotalValidJoints, SphereShapeCaches.Num(), CapsuleShapeCaches.Num(), PlaneShapeCaches.Num());
#endif
}

#if !UE_BUILD_SHIPPING
void FAnimNode_VRMSpringBone::DrawSpringBones(FPoseContext& Output, FCSPose<FCompactPose>& CSPose, int32 SpringDrawMode) const
{
    const FColor ChainLineColor(0,200,255);
    const FColor TipColor(255,220,0);
    const FColor RestDirColor(128,128,255);
    const FColor SimDirColor(0,255,128);
    const FColor CenterColor = FColor::White;
    
    for (const FVRMSBChainCache& Chain : ChainCaches)
    {
        if (SpringDrawMode >= 2 && Chain.bHasCenter && Chain.CenterBoneIndex.IsValid())
        {
            const FVector CenterPos = CSPose.GetComponentSpaceTransform(Chain.CenterBoneIndex).GetLocation();
            Output.AnimInstanceProxy->AnimDrawDebugSphere(CenterPos, 2.f, 8, CenterColor, false, 0.f);
        }
        for (int32 j=0; j<Chain.Joints.Num(); ++j)
        {
            const FVRMSBJointCache& JC = Chain.Joints[j];
            if (!JC.bValid || JC.RestLength <= KINDA_SMALL_NUMBER) continue;
            if (!JC.BoneIndex.IsValid()) continue;
            const FVector ParentPos = CSPose.GetComponentSpaceTransform(JC.BoneIndex).GetLocation();
            const FVector TipPos = JC.CurrentTail;  // Updated to use CurrentTail
            Output.AnimInstanceProxy->AnimDrawDebugLine(ParentPos, TipPos, ChainLineColor, false, 0.f, 0.f, SDPG_World);
            const float DrawRadius = FMath::Max(1.f, JC.HitRadius);
            Output.AnimInstanceProxy->AnimDrawDebugSphere(TipPos, DrawRadius, 8, TipColor, false, 0.f);
            if (SpringDrawMode >= 2)
            {
                const FVector RestEnd = ParentPos + JC.RestDirection * JC.RestLength;
                Output.AnimInstanceProxy->AnimDrawDebugLine(ParentPos, RestEnd, RestDirColor, false, 0.f, 0.f, SDPG_World);
                const FVector SimDir = (TipPos - ParentPos).GetSafeNormal();
                const FVector SimEnd = ParentPos + SimDir * JC.RestLength;
                Output.AnimInstanceProxy->AnimDrawDebugLine(ParentPos, SimEnd, SimDirColor, false, 0.f, 0.f, SDPG_World);
            }
        }
    }
}

void FAnimNode_VRMSpringBone::DrawColliders(FPoseContext& Output) const
{
    const FColor SphereColorOutside(0,255,255);
    const FColor SphereColorInside(255,0,200);
    const FColor CapsuleColorOutside(0,255,180);
    const FColor CapsuleColorInside(255,80,255);
    const FColor PlaneColor(200,255,0);
    
    // Draw sphere colliders
    for (int32 i=0; i<SphereShapeCaches.Num(); ++i)
    {
        const FVRMSBSphereShapeCache& SC = SphereShapeCaches[i];
        if (!SC.bValid) continue;
        const FVector C = SphereWorldPos.IsValidIndex(i) ? SphereWorldPos[i] : FVector::ZeroVector;
        const FColor Col = SC.bInside ? SphereColorInside : SphereColorOutside;
        const float DrawRadius = FMath::Max(0.5f, SC.Radius);
        Output.AnimInstanceProxy->AnimDrawDebugSphere(C, DrawRadius, 12, Col, false, 0.f);
    }
    
    // Draw capsule colliders
    for (int32 i=0; i<CapsuleShapeCaches.Num(); ++i)
    {
        const FVRMSBCapsuleShapeCache& CC = CapsuleShapeCaches[i];
        if (!CC.bValid) continue;
        const FVector P0 = CapsuleWorldP0.IsValidIndex(i) ? CapsuleWorldP0[i] : FVector::ZeroVector;
        const FVector P1 = CapsuleWorldP1.IsValidIndex(i) ? CapsuleWorldP1[i] : FVector::ZeroVector;
        const FColor Col = CC.bInside ? CapsuleColorInside : CapsuleColorOutside;
        const float R = FMath::Max(0.5f, CC.Radius);
        
        // Draw the line connecting the centers
        Output.AnimInstanceProxy->AnimDrawDebugLine(P0, P1, Col, false, 0.f, 0.f, SDPG_World);
        // Draw spheres at both ends
        Output.AnimInstanceProxy->AnimDrawDebugSphere(P0, R, 8, Col, false, 0.f);
        Output.AnimInstanceProxy->AnimDrawDebugSphere(P1, R, 8, Col, false, 0.f);
    }
    
    // Draw plane colliders
    for (int32 i=0; i<PlaneShapeCaches.Num(); ++i)
    {
        const FVRMSBPlaneShapeCache& PC = PlaneShapeCaches[i];
        if (!PC.bValid) continue;
        const FVector P = PlaneWorldPoint.IsValidIndex(i) ? PlaneWorldPoint[i] : FVector::ZeroVector;
        const FVector N = PlaneWorldNormal.IsValidIndex(i) ? PlaneWorldNormal[i] : FVector::UpVector;
        const float L = 8.f;
        
        // Create perpendicular vectors for plane visualization
        FVector X = FVector::CrossProduct(N, FVector::UpVector).GetSafeNormal();
        if (X.IsNearlyZero()) {
            X = FVector::CrossProduct(N, FVector::ForwardVector).GetSafeNormal();
        }
        const FVector Y = FVector::CrossProduct(N, X).GetSafeNormal();
        
        // Draw plane as a cross pattern
        Output.AnimInstanceProxy->AnimDrawDebugLine(P - X*L, P + X*L, PlaneColor, false, 0.f, 0.f, SDPG_World);
        Output.AnimInstanceProxy->AnimDrawDebugLine(P - Y*L, P + Y*L, PlaneColor, false, 0.f, 0.f, SDPG_World);
        Output.AnimInstanceProxy->AnimDrawDebugLine(P, P + N*L, PlaneColor, false, 0.f, 0.f, SDPG_World);
    }
}
#endif

