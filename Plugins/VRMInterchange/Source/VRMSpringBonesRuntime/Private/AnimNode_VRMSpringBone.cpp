#include "AnimNode_VRMSpringBone.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "VRMInterchangeLog.h"

#define LOCTEXT_NAMESPACE "AnimNode_VRMSpringBone"

static FVector ResolveSpringGravity(const FVRMSpring& Spring, const FVector& DefaultGravity)
{
    const FVector Dir = Spring.GravityDir.GetSafeNormal();
    return DefaultGravity + Dir * Spring.GravityPower;
}

// Static test helper
void FAnimNode_VRMSpringBone::ComputeRestFromReferenceSkeleton(const FReferenceSkeleton& RefSkel, FChainInfo& Chain)
{
    const int32 NumBones = RefSkel.GetNum(); if (NumBones <= 0) { Chain.SegmentRestLengths.Reset(); Chain.RestDirections.Reset(); Chain.RestComponentPositions.Reset(); return; }
    const TArray<FTransform>& RefLocal = RefSkel.GetRefBonePose();
    TArray<FTransform> RefComponent; RefComponent.SetNum(NumBones);
    for (int32 BoneIdx=0; BoneIdx<NumBones; ++BoneIdx)
    {
        const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
        RefComponent[BoneIdx] = (ParentIdx >= 0) ? (RefLocal[BoneIdx] * RefComponent[ParentIdx]) : RefLocal[BoneIdx];
    }

    auto RefIndex = [&](const FBoneReference& BoneRef)->int32 { return BoneRef.BoneName.IsNone()? INDEX_NONE : RefSkel.FindBoneIndex(BoneRef.BoneName); };

    const int32 Count = Chain.BoneRefs.Num();
    Chain.SegmentRestLengths.SetNum(Count);
    Chain.RestDirections.SetNum(Count);
    Chain.RestComponentPositions.SetNum(Count);

    if (Count == 0) return;

    const int32 RootRef = RefIndex(Chain.BoneRefs[0]);
    Chain.SegmentRestLengths[0] = 0.f; Chain.RestDirections[0] = FVector::ZeroVector; Chain.RestComponentPositions[0] = (RootRef!=INDEX_NONE && RootRef<NumBones)? RefComponent[RootRef].GetLocation(): FVector::ZeroVector;

    for (int32 i=1;i<Count;++i)
    {
        const int32 PIdx = RefIndex(Chain.BoneRefs[i-1]); const int32 CIdx = RefIndex(Chain.BoneRefs[i]);
        FVector Dir = FVector::ZeroVector; float Len = 0.f; FVector ParentPos = FVector::ZeroVector; FVector ChildPos = FVector::ZeroVector;
        if (PIdx!=INDEX_NONE && CIdx!=INDEX_NONE && PIdx<NumBones && CIdx<NumBones)
        { ParentPos = RefComponent[PIdx].GetLocation(); ChildPos  = RefComponent[CIdx].GetLocation(); Dir = (ChildPos - ParentPos); Len = Dir.Size(); if (Len > KINDA_SMALL_NUMBER) Dir /= Len; else { Dir = FVector::ZeroVector; Len = 0.f; } }
        Chain.SegmentRestLengths[i] = Len; Chain.RestDirections[i] = Dir; Chain.RestComponentPositions[i] = ChildPos;
    }
}

void FAnimNode_VRMSpringBone::ComputeRestFromPositions(FChainInfo& Chain)
{
    const int32 Count = Chain.RestComponentPositions.Num();
    Chain.SegmentRestLengths.SetNum(Count);
    Chain.RestDirections.SetNum(Count);
    if (Count == 0) return;
    Chain.SegmentRestLengths[0] = 0.f; Chain.RestDirections[0] = FVector::ZeroVector;
    for (int32 i=1;i<Count;++i)
    {
        const FVector ParentPos = Chain.RestComponentPositions[i-1];
        const FVector ChildPos  = Chain.RestComponentPositions[i];
        FVector Dir = (ChildPos - ParentPos); const float Len = Dir.Size();
        Chain.SegmentRestLengths[i] = Len; Chain.RestDirections[i] = (Len > KINDA_SMALL_NUMBER)? (Dir/Len) : FVector::ZeroVector;
    }
}

void FAnimNode_VRMSpringBone::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
    FAnimNode_Base::Initialize_AnyThread(Context);
    ComponentPose.Initialize(Context);
    Chains.Reset();
    bPendingTeleportReset = false;
    if (SpringConfig) { CachedSourceHash = SpringConfig->SourceHash; }
}

void FAnimNode_VRMSpringBone::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
    ComponentPose.CacheBones(Context);
    const FBoneContainer& BoneContainer = Context.AnimInstanceProxy->GetRequiredBones();
    if (NeedsRebuild()) { BuildChains(BoneContainer); ComputeReferencePoseRestLengths(BoneContainer); }
}

void FAnimNode_VRMSpringBone::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    ComponentPose.Update(Context);
    LastDeltaTime = Context.GetDeltaTime();

    // Teleport / hitch detection
    if (bResetOnTeleport && LastDeltaTime >= TeleportResetTime) { bPendingTeleportReset = true; }
    if (bForceReset) { bPendingTeleportReset = true; bForceReset = false; }

    if (RuntimeNeedsRebuild())
    {
        for (FChainInfo& Chain : Chains) { Chain.ResetRuntimeState(); }
        Chains.Reset();
        if (SpringConfig) { CachedSourceHash = SpringConfig->SourceHash; }
    }
}

void FAnimNode_VRMSpringBone::Evaluate_AnyThread(FPoseContext& Output)
{
    ComponentPose.Evaluate(Output);

    // Seed when simulation disabled
    if (!bEnableSimulation)
    {
        for (FChainInfo& Chain : Chains)
        {
            if (!Chain.bInitializedPositions && Chain.RestComponentPositions.Num() == Chain.CompactIndices.Num())
            {
                Chain.CurrPositions = Chain.RestComponentPositions;
                Chain.PrevPositions = Chain.RestComponentPositions;
                Chain.bInitializedPositions = true;
            }
        }
        return;
    }

    if (Chains.Num() == 0) return;

    FCSPose<FCompactPose> CSPose; CSPose.InitPose(Output.Pose);

    for (FChainInfo& Chain : Chains)
    {
        const int32 Count = Chain.CompactIndices.Num(); if (Count == 0) continue;

        if (!Chain.bInitializedPositions || bPendingTeleportReset)
        {
            Chain.CurrPositions.SetNum(Count); Chain.PrevPositions.SetNum(Count);
            for (int32 i=0;i<Count;++i)
            { Chain.CurrPositions[i] = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i]).GetLocation(); Chain.PrevPositions[i] = Chain.CurrPositions[i]; }
            Chain.bInitializedPositions = true; bPendingTeleportReset = false; continue;
        }

        // Resolve per-spring params
        const float Stiff = FMath::Clamp(Chain.SpringStiffness, 0.f, 1.f);
        const float Damp  = FMath::Clamp(1.f - FMath::Max(Chain.SpringDrag, 0.f), 0.f, 1.f);
        const FVector Grav = Gravity + Chain.SpringGravity; // compose base gravity and spring gravity

        // Optionally resolve center compact index on first use
        if (Chain.CenterCompactIndex == FCompactPoseBoneIndex(INDEX_NONE) && !Chain.CenterBoneName.IsNone())
        {
            FBoneReference CenterRef; CenterRef.BoneName = Chain.CenterBoneName; CenterRef.Initialize(Output.Pose.GetBoneContainer());
            if (CenterRef.IsValidToEvaluate(Output.Pose.GetBoneContainer()))
            {
                Chain.CenterCompactIndex = CenterRef.GetCompactPoseIndex(Output.Pose.GetBoneContainer());
                Chain.CenterPullStrength = (GlobalCenterPullStrength > 0.f) ? GlobalCenterPullStrength : 0.f;
            }
        }

        // Root pinned to animated
        Chain.CurrPositions[0] = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[0]).GetLocation();
        Chain.PrevPositions[0] = Chain.CurrPositions[0];

        // Substeps
        float Remaining = LastDeltaTime; const float StepTarget = FMath::Max(0.0001f, MaxSubstepDeltaTime); int32 Steps = 0;
        while (Remaining > KINDA_SMALL_NUMBER && Steps < MaxSubsteps)
        {
            const float Dt = FMath::Min(Remaining, StepTarget); Remaining -= Dt; ++Steps;
            for (int32 i=1;i<Count;++i)
            {
                const FVector Animated = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i]).GetLocation();
                const FVector Cur = Chain.CurrPositions[i];
                const FVector Prev = Chain.PrevPositions[i];
                const FVector Vel = (Cur - Prev) * Damp;
                const FVector SpringToAnim = (Animated - Cur) * Stiff;
                FVector CenterPull = FVector::ZeroVector;
                if (Chain.CenterCompactIndex != FCompactPoseBoneIndex(INDEX_NONE) && Chain.CenterPullStrength > 0.f)
                {
                    const FVector CenterPos = CSPose.GetComponentSpaceTransform(Chain.CenterCompactIndex).GetLocation();
                    CenterPull = (CenterPos - Cur) * Chain.CenterPullStrength; // crude center pull
                }
                const FVector GravityAccel = Grav * Dt * Dt;
                const FVector Next = Cur + Vel + (SpringToAnim + CenterPull) * Dt + GravityAccel;
                Chain.PrevPositions[i] = Cur; Chain.CurrPositions[i] = Next;
            }
            // Constraint
            for (int32 i=1;i<Count;++i)
            {
                const float RestLen = (i < Chain.SegmentRestLengths.Num()) ? Chain.SegmentRestLengths[i] : 0.f; if (RestLen <= KINDA_SMALL_NUMBER) continue;
                FVector& ParentPos = Chain.CurrPositions[i-1]; FVector& ChildPos = Chain.CurrPositions[i];
                FVector Delta = ChildPos - ParentPos; const float CurLen = Delta.Size();
                if (CurLen <= KINDA_SMALL_NUMBER)
                { const FVector Fallback = Chain.RestDirections.IsValidIndex(i) ? Chain.RestDirections[i] * RestLen : FVector(RestLen,0,0); ChildPos = ParentPos + Fallback; continue; }
                const float Diff = (CurLen - RestLen) / CurLen; ChildPos -= Delta * Diff;
            }
        }

        // Write back rotations
        FCompactPose& Pose = Output.Pose;
        for (int32 i=1;i<Count;++i)
        {
            const FVector ParentSimPos = Chain.CurrPositions[i-1]; const FVector ChildSimPos  = Chain.CurrPositions[i];
            const FVector SimDir = (ChildSimPos - ParentSimPos).GetSafeNormal(); if (SimDir.IsNearlyZero()) continue;
            const FVector RefDir = Chain.RestDirections.IsValidIndex(i)? Chain.RestDirections[i] : SimDir; if (RefDir.IsNearlyZero()) continue;
            const FQuat DeltaRot = FQuat::FindBetweenNormals(RefDir, SimDir);
            const FCompactPoseBoneIndex ChildIdx = Chain.CompactIndices[i]; FTransform LocalBone = Pose[ChildIdx];
            LocalBone.SetRotation( (DeltaRot * LocalBone.GetRotation()).GetNormalized() ); Pose[ChildIdx] = LocalBone;
        }
    }
}

void FAnimNode_VRMSpringBone::GatherDebugData(FNodeDebugData& DebugData)
{
    int32 TotalJoints=0, Missing=0; for (const FChainInfo& C : Chains){ TotalJoints += C.CompactIndices.Num(); Missing += C.MissingJointCount; }
    const TCHAR* SimText = bEnableSimulation? TEXT("On") : TEXT("Off");
    FString Line = FString::Printf(TEXT("VRMSpringBone (Springs=%d Resolved=%d Missing=%d Sim=%s Steps=%d Dt=%.3f)"), Chains.Num(), TotalJoints, Missing, SimText, MaxSubsteps, LastDeltaTime);
    DebugData.AddDebugItem(Line);
    ComponentPose.GatherDebugData(DebugData);
}

bool FAnimNode_VRMSpringBone::NeedsRebuild() const
{
    if (!SpringConfig) { return Chains.Num() != 0; }
    return CachedSourceHash != SpringConfig->SourceHash || Chains.Num() != SpringConfig->SpringConfig.Springs.Num();
}

bool FAnimNode_VRMSpringBone::RuntimeNeedsRebuild() const
{
    if (!SpringConfig) { return Chains.Num() != 0; }
    return (CachedSourceHash != SpringConfig->SourceHash) || (Chains.Num() != SpringConfig->SpringConfig.Springs.Num());
}

void FAnimNode_VRMSpringBone::BuildChains(const FBoneContainer& BoneContainer)
{
    Chains.Reset();
    if (!SpringConfig || !SpringConfig->SpringConfig.IsValid()) return;
    const FVRMSpringConfig& Config = SpringConfig->SpringConfig; Chains.Reserve(Config.Springs.Num());
    for (int32 SpringIdx=0; SpringIdx<Config.Springs.Num(); ++SpringIdx)
    {
        const FVRMSpring& Spring = Config.Springs[SpringIdx];
        FChainInfo Chain; Chain.IntendedJointCount = Spring.JointIndices.Num(); Chain.SourceSpringIndex = SpringIdx;
        // Flatten per-spring params
        Chain.SpringStiffness = Spring.Stiffness > 0.f ? Spring.Stiffness : GlobalStiffness;
        Chain.SpringDrag      = Spring.Drag > 0.f ? Spring.Drag : GlobalDamping;
        Chain.SpringGravity   = ResolveSpringGravity(Spring, FVector::ZeroVector);
        Chain.HitRadius       = Spring.HitRadius;
        Chain.CenterBoneName  = Spring.CenterBoneName;
        Chain.CenterCompactIndex = FCompactPoseBoneIndex(INDEX_NONE);
        Chain.CenterPullStrength = GlobalCenterPullStrength;

        for (int32 JointIdx : Spring.JointIndices)
        {
            if (!Config.Joints.IsValidIndex(JointIdx)) { ++Chain.MissingJointCount; continue; }
            const FVRMSpringJoint& J = Config.Joints[JointIdx]; if (J.BoneName.IsNone()) { ++Chain.MissingJointCount; continue; }
            FBoneReference Ref; Ref.BoneName = J.BoneName; Ref.Initialize(BoneContainer);
            if (Ref.IsValidToEvaluate(BoneContainer)) { Chain.CompactIndices.Add(Ref.GetCompactPoseIndex(BoneContainer)); Chain.BoneRefs.Add(MoveTemp(Ref)); }
            else { ++Chain.MissingJointCount; }
        }
        if (Chain.CompactIndices.Num() > 0) { Chains.Add(MoveTemp(Chain)); }
    }
    if (SpringConfig) { CachedSourceHash = SpringConfig->SourceHash; }
}

void FAnimNode_VRMSpringBone::ComputeReferencePoseRestLengths(const FBoneContainer& BoneContainer)
{
    const FReferenceSkeleton& RefSkel = BoneContainer.GetReferenceSkeleton();
    const int32 NumBones = RefSkel.GetNum(); if (NumBones == 0) return;

    const TArray<FTransform>& RefLocal = RefSkel.GetRefBonePose();
    TArray<FTransform> RefComponent; RefComponent.SetNum(NumBones);
    for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
    {
        const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
        RefComponent[BoneIdx] = (ParentIdx >= 0) ? (RefLocal[BoneIdx] * RefComponent[ParentIdx]) : RefLocal[BoneIdx];
    }

    auto RefIndex = [&](const FBoneReference& BoneRef)->int32 { return BoneRef.BoneName.IsNone()?INDEX_NONE: RefSkel.FindBoneIndex(BoneRef.BoneName); };

    for (FChainInfo& Chain : Chains)
    {
        const int32 Count = Chain.CompactIndices.Num();
        Chain.SegmentRestLengths.SetNum(Count);
        Chain.RestDirections.SetNum(Count);
        Chain.RestComponentPositions.SetNum(Count);
        if (Count == 0) continue;
        const int32 RootRef = (Count>0)? RefIndex(Chain.BoneRefs[0]) : INDEX_NONE;
        Chain.SegmentRestLengths[0] = 0.f; Chain.RestDirections[0] = FVector::ZeroVector; Chain.RestComponentPositions[0] = (RootRef!=INDEX_NONE)? RefComponent[RootRef].GetLocation(): FVector::ZeroVector;
        for (int32 i=1;i<Count;++i)
        {
            const int32 PIdx = RefIndex(Chain.BoneRefs[i-1]); const int32 CIdx = RefIndex(Chain.BoneRefs[i]);
            FVector Dir = FVector::ZeroVector; float Len = 0.f; FVector ParentPos = FVector::ZeroVector; FVector ChildPos = FVector::ZeroVector;
            if (PIdx!=INDEX_NONE && CIdx!=INDEX_NONE && PIdx<NumBones && CIdx<NumBones)
            { ParentPos = RefComponent[PIdx].GetLocation(); ChildPos  = RefComponent[CIdx].GetLocation(); Dir = (ChildPos - ParentPos); Len = Dir.Size(); if (Len > KINDA_SMALL_NUMBER) Dir /= Len; else { Dir = FVector::ZeroVector; Len = 0.f; } }
            Chain.SegmentRestLengths[i] = Len; Chain.RestDirections[i] = Dir; Chain.RestComponentPositions[i] = ChildPos;
        }
    }
}

#undef LOCTEXT_NAMESPACE
