#include "AnimNode_VRMSpringBone.h"
#include "Animation/AnimInstanceProxy.h"
#include "AnimationRuntime.h"
#include "VRMInterchangeLog.h"
#include "HAL/IConsoleManager.h"
#include "Engine/EngineTypes.h"
#include "Animation/AnimInstance.h"
#include "UObject/SoftObjectPtr.h"
#include "Components/SkeletalMeshComponent.h"

#define LOCTEXT_NAMESPACE "AnimNode_VRMSpringBone"

// Debug CVars
static TAutoConsoleVariable<int32> CVarVRMSpringDebug(
    TEXT("vrm.Spring.Debug"),
    0,
    TEXT("Enable verbose VRM spring debug logging (0=off,1=summary,2=verbose)."),
    ECVF_Default);

static TAutoConsoleVariable<int32> CVarVRMSpringDraw(
    TEXT("vrm.Spring.Draw"),
    0,
    TEXT("Draw spring chains in-world from the anim node (0=off,1=positions,2=positions+animated+colliders)."),
    ECVF_Default);

static FVector ResolveSpringGravity(const FVRMSpring& Spring, const FVector& DefaultGravity)
{
    const FVector Dir = Spring.GravityDir.GetSafeNormal();
    return DefaultGravity + Dir * Spring.GravityPower;
}

// Helper: try to pull SpringConfig from the owning AnimInstance when binding didn't set it (PP ABP edge cases)
static UVRMSpringBoneData* TryAdoptSpringConfigFromAnimInstance(FAnimInstanceProxy* Proxy)
{
    if (!Proxy) return nullptr;
    UAnimInstance* AnimInst = Cast<UAnimInstance>(Proxy->GetAnimInstanceObject());
    if (!AnimInst) return nullptr;

    UClass* Cls = AnimInst->GetClass();

    if (FObjectProperty* ObjProp = FindFProperty<FObjectProperty>(Cls, TEXT("SpringConfig")))
    {
        if (ObjProp->PropertyClass && ObjProp->PropertyClass->IsChildOf(UVRMSpringBoneData::StaticClass()))
        {
            return Cast<UVRMSpringBoneData>(ObjProp->GetObjectPropertyValue_InContainer(AnimInst));
        }
    }
    if (FSoftObjectProperty* SoftProp = FindFProperty<FSoftObjectProperty>(Cls, TEXT("SpringConfig")))
    {
        const FSoftObjectPtr* SoftPtr = SoftProp->GetPropertyValuePtr_InContainer(AnimInst);
        if (SoftPtr)
        {
            UObject* Obj = SoftPtr->Get();
            if (!Obj)
            {
                const FSoftObjectPath& Path = SoftPtr->ToSoftObjectPath();
                if (Path.IsValid())
                {
                    Obj = Path.TryLoad();
                }
            }
            return Cast<UVRMSpringBoneData>(Obj);
        }
    }
    return nullptr;
}

// Update helper: recompute rest lengths/directions from the current component-space animated transforms.
// This ensures the physical rest lengths respect any runtime or import-time bone scaling / mirroring.
static void UpdateChainRestFromCSPose(FAnimNode_VRMSpringBone::FChainInfo& Chain, FCSPose<FCompactPose>& CSPose)
{
    const int32 Count = Chain.CompactIndices.Num();
    Chain.SegmentRestLengths.SetNum(Count);
    Chain.RestDirections.SetNum(Count);
    Chain.RestComponentPositions.SetNum(Count);

    if (Count == 0) return;

    // Root
    const FCompactPoseBoneIndex RootIdx = Chain.CompactIndices[0];
    const FVector RootPos = CSPose.GetComponentSpaceTransform(RootIdx).GetLocation();
    Chain.SegmentRestLengths[0] = 0.f;
    Chain.RestDirections[0] = FVector::ZeroVector;
    Chain.RestComponentPositions[0] = RootPos;

    for (int32 i = 1; i < Count; ++i)
    {
        const FCompactPoseBoneIndex PIdx = Chain.CompactIndices[i - 1];
        const FCompactPoseBoneIndex CIdx = Chain.CompactIndices[i];
        const FVector ParentPos = CSPose.GetComponentSpaceTransform(PIdx).GetLocation();
        const FVector ChildPos = CSPose.GetComponentSpaceTransform(CIdx).GetLocation();
        const FVector Dir = (ChildPos - ParentPos);
        const float Len = Dir.Size();
        Chain.SegmentRestLengths[i] = Len;
        Chain.RestDirections[i] = (Len > KINDA_SMALL_NUMBER) ? (Dir / Len) : FVector::ZeroVector;
        Chain.RestComponentPositions[i] = ChildPos;
    }
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

// Math helpers
FVector FAnimNode_VRMSpringBone::ProjectPointOnSegment(const FVector& A, const FVector& B, const FVector& P)
{
    const FVector AB = B - A; const float AB2 = AB.SizeSquared(); if (AB2 <= KINDA_SMALL_NUMBER) return A;
    const float T = FMath::Clamp(FVector::DotProduct(P - A, AB) / AB2, 0.f, 1.f);
    return A + AB * T;
}

static void ResolveSphereCollision(FVector& JointPos, FVector& PrevPos, const FVector& ColliderPos, float ColliderRadius, float JointRadius, float Friction, float Restitution)
{
    const float MinDist = ColliderRadius + JointRadius;
    FVector Delta = JointPos - ColliderPos; float Dist = Delta.Size();
    if (Dist <= KINDA_SMALL_NUMBER)
    {
        // Push out along arbitrary axis
        Delta = FVector(MinDist, 0.f, 0.f);
        JointPos = ColliderPos + Delta;
        PrevPos = JointPos; // clear velocity
        return;
    }
    if (Dist < MinDist)
    {
        const FVector N = Delta / Dist;
        const FVector Target = ColliderPos + N * MinDist;

        // Compute velocity before correction
        FVector Vel = JointPos - PrevPos;

        // Decompose into normal and tangential components
        const float Vn = FVector::DotProduct(Vel, N);
        const FVector VnVec = N * Vn;
        const FVector VtVec = Vel - VnVec;

        // Apply restitution on normal, friction on tangent
        FVector NewVel = (-Restitution * VnVec) + ((1.f - Friction) * VtVec);

        JointPos = Target;
        PrevPos = JointPos - NewVel;
    }
}

// Capsule collision: treat as infinite cylinder capped by spheres, project onto segment and resolve like sphere at closest point
static void ResolveCapsuleCollision(FVector& JointPos, FVector& PrevPos, const FVector& A, const FVector& B, float CapsuleRadius, float JointRadius, float Friction, float Restitution)
{
    const FVector Closest = FAnimNode_VRMSpringBone::ProjectPointOnSegment(A, B, JointPos);
    ResolveSphereCollision(JointPos, PrevPos, Closest, CapsuleRadius, JointRadius, Friction, Restitution);
}

void FAnimNode_VRMSpringBone::ResolveCapsuleCollisionTestHook(FVector& JointPos, FVector& PrevPos, const FVector& A, const FVector& B, float CapsuleRadius, float JointRadius, float Friction, float Restitution)
{
    ResolveCapsuleCollision(JointPos, PrevPos, A, B, CapsuleRadius, JointRadius, Friction, Restitution);
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

    // Fallback adoption: PP ABP binding can fail before first CacheBones; try to read the AnimInstance variable here
    if (!SpringConfig)
    {
        if (UVRMSpringBoneData* Adopted = TryAdoptSpringConfigFromAnimInstance(Context.AnimInstanceProxy))
        {
            SpringConfig = Adopted;
            CachedSourceHash = SpringConfig ? SpringConfig->SourceHash : CachedSourceHash;
            UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring] CacheBones: Adopted SpringConfig from AnimInstance: %s"), SpringConfig ? *SpringConfig->GetName() : TEXT("<null>"));
        }
    }

    const int32 DebugLevel = CVarVRMSpringDebug.GetValueOnAnyThread();
    if (DebugLevel >= 1)
    {
        UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring] CacheBones: SpringConfig=%s Chains=%d NeedsRebuild=%s"),
            SpringConfig? *SpringConfig->GetName() : TEXT("<null>"), Chains.Num(), NeedsRebuild()? TEXT("true") : TEXT("false"));
    }

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

    const int32 DebugLevel = CVarVRMSpringDebug.GetValueOnAnyThread();
    const int32 DrawLevel  = CVarVRMSpringDraw.GetValueOnAnyThread();

    // Late adoption in case binding populated after Initialize
    if (!SpringConfig)
    {
        if (UVRMSpringBoneData* Adopted = TryAdoptSpringConfigFromAnimInstance(Output.AnimInstanceProxy))
        {
            SpringConfig = Adopted;
            CachedSourceHash = SpringConfig ? SpringConfig->SourceHash : CachedSourceHash;
            if (DebugLevel >= 1)
            {
                UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring] Evaluate: Adopted SpringConfig from AnimInstance: %s"), SpringConfig ? *SpringConfig->GetName() : TEXT("<null>"));
            }
        }
    }

    if (DebugLevel >= 2)
    {
        const TCHAR* CfgName = SpringConfig ? *SpringConfig->GetName() : TEXT("<null>");
        const bool bCfgValid = SpringConfig && SpringConfig->SpringConfig.IsValid();
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring] Evaluate: Pre-check Chains=%d SpringConfig=%s IsValid=%s NeedsRebuild=%s"),
            Chains.Num(), CfgName, bCfgValid?TEXT("true"):TEXT("false"), NeedsRebuild()?TEXT("true"):TEXT("false"));
    }

    // Opportunistic runtime build if chains are missing
    if (Chains.Num() == 0)
    {
        if (SpringConfig && SpringConfig->SpringConfig.IsValid())
        {
            if (DebugLevel >= 1)
            {
                UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring] Evaluate: Chains empty, attempting on-the-fly BuildChains."));
            }
            BuildChains(Output.Pose.GetBoneContainer());
            ComputeReferencePoseRestLengths(Output.Pose.GetBoneContainer());
        }
        else if (DebugLevel >= 1)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring] Evaluate: Chains empty and SpringConfig is %s/%s."),
                SpringConfig?TEXT("present"):TEXT("null"),
                (SpringConfig && SpringConfig->SpringConfig.IsValid())?TEXT("valid"):TEXT("invalid"));
        }
    }

    // Seed when simulation disabled
    if (!bEnableSimulation)
    {
        if (DebugLevel >= 2)
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring] Evaluate: simulation disabled; Chains=%d"), Chains.Num());
        }
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

    if (Chains.Num() == 0)
    {
        if (DebugLevel >= 1)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring] Evaluate: no Chains; check BuildChains and data asset binding."));
        }
        return;
    }

    FCSPose<FCompactPose> CSPose; CSPose.InitPose(Output.Pose);

    // Precompute collider world/component-space positions for VRM colliders
    TArray<FVector> ColliderSpherePositionsCS;
    TArray<float>   ColliderSphereRadii;
    struct FCapsuleTmp { FVector A; FVector B; float R; };
    TArray<FCapsuleTmp> ColliderCapsulesCS;
    ColliderSpherePositionsCS.Reserve(64);
    ColliderSphereRadii.Reserve(64);
    ColliderCapsulesCS.Reserve(32);

    USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy ? Output.AnimInstanceProxy->GetSkelMeshComponent() : nullptr;
    // Note: VRM data expresses colliders in meters; apply the same scale to offsets and radii.
    const float UnitScale = (bVRMRadiiInMeters ? VRMToCentimetersScale : 1.f);

    // Prefilter by ColliderGroups per-spring: collect referenced indices to reduce checks
    TSet<int32> UsedColliderIndices;

    if (SpringConfig && SpringConfig->SpringConfig.IsValid())
    {
        const FVRMSpringConfig& Config = SpringConfig->SpringConfig;

        // Accumulate groups used by any existing chains' source spring
        for (const FChainInfo& Chain : Chains)
        {
            const int32 SpringIdx = Chain.SourceSpringIndex;
            if (Config.Springs.IsValidIndex(SpringIdx))
            {
                for (int32 GIdx : Config.Springs[SpringIdx].ColliderGroupIndices)
                {
                    if (Config.ColliderGroups.IsValidIndex(GIdx))
                    {
                        for (int32 CIdx : Config.ColliderGroups[GIdx].ColliderIndices)
                        {
                            UsedColliderIndices.Add(CIdx);
                        }
                    }
                }
            }
        }

        // Build collider shapes in component space (spheres + capsules)
        for (int32 CIdx = 0; CIdx < Config.Colliders.Num(); ++CIdx)
        {
            if (UsedColliderIndices.Num() > 0 && !UsedColliderIndices.Contains(CIdx))
            {
                continue; // filtered out
            }

            const FVRMSpringCollider& C = Config.Colliders[CIdx];

            FTransform ColliderBoneCS = FTransform::Identity;
            bool bHaveBone = false;
            if (!C.BoneName.IsNone() && SkelComp)
            {
                const int32 BoneIndex = SkelComp->GetBoneIndex(C.BoneName);
                if (BoneIndex != INDEX_NONE)
                {
                    // Get bone transform in world space then convert to component space to ensure correct space for simulation/drawing
                    const FTransform BoneWS = SkelComp->GetBoneTransform(BoneIndex);
                    const FTransform C2W = SkelComp->GetComponentToWorld();
                    ColliderBoneCS = BoneWS.GetRelativeTransform(C2W);
                    bHaveBone = true;
                }
            }

            // Spheres
            for (const FVRMSpringColliderSphere& S : C.Spheres)
            {
                const FVector Local = S.Offset * UnitScale;
                FVector SphereCSPos = bHaveBone ? ColliderBoneCS.TransformPosition(Local) : Local;
                ColliderSpherePositionsCS.Add(SphereCSPos);
                ColliderSphereRadii.Add(S.Radius * UnitScale);
            }

            // Capsules
            for (const FVRMSpringColliderCapsule& Cap : C.Capsules)
            {
                const FVector ALocal = Cap.Offset * UnitScale;
                const FVector BLocal = Cap.TailOffset * UnitScale;
                const FVector A = bHaveBone ? ColliderBoneCS.TransformPosition(ALocal) : ALocal;
                const FVector B = bHaveBone ? ColliderBoneCS.TransformPosition(BLocal) : BLocal;
                FCapsuleTmp Tmp; Tmp.A = A; Tmp.B = B; Tmp.R = Cap.Radius * UnitScale;
                ColliderCapsulesCS.Add(Tmp);
            }
        }
    }

    for (FChainInfo& Chain : Chains)
    {
        const int32 Count = Chain.CompactIndices.Num(); if (Count == 0) continue;

        if (!Chain.bInitializedPositions || bPendingTeleportReset)
        {
            // Initialize simulated positions from component-space animated transforms. Also recompute rest lengths/directions
            Chain.CurrPositions.SetNum(Count); Chain.PrevPositions.SetNum(Count);
            for (int32 i=0;i<Count;++i)
            { Chain.CurrPositions[i] = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i]).GetLocation(); Chain.PrevPositions[i] = Chain.CurrPositions[i]; }

            // Recompute rest information based on current animated (component-space) transforms.
            UpdateChainRestFromCSPose(Chain, CSPose);

            Chain.bInitializedPositions = true; bPendingTeleportReset = false; if (DebugLevel >= 2) { UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring] Initialize positions for chain; Joints=%d"), Count); } continue;
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

            // Collisions: sphere + capsule
            if (ColliderSpherePositionsCS.Num() > 0 || ColliderCapsulesCS.Num() > 0)
            {
                for (int32 j=1;j<Count;++j)
                {
                    const float JointRadius = (Chain.JointHitRadii.IsValidIndex(j) ? Chain.JointHitRadii[j] : Chain.HitRadius) * UnitScale;

                    for (int32 c=0;c<ColliderSpherePositionsCS.Num();++c)
                    {
                        ResolveSphereCollision(Chain.CurrPositions[j], Chain.PrevPositions[j], ColliderSpherePositionsCS[c], ColliderSphereRadii[c], JointRadius, CollisionFriction, CollisionRestitution);
                    }
                    for (const FCapsuleTmp& Cap : ColliderCapsulesCS)
                    {
                        ResolveCapsuleCollision(Chain.CurrPositions[j], Chain.PrevPositions[j], Cap.A, Cap.B, Cap.R, JointRadius, CollisionFriction, CollisionRestitution);
                    }
                }
            }
        }

        // Optional debug draw
        if (DrawLevel > 0)
        {
            const FColor ChainColor = FColor::Cyan;
            const FTransform C2W = SkelComp ? SkelComp->GetComponentToWorld() : FTransform::Identity;
            auto ToWorld = [&](const FVector& P) -> FVector { return C2W.TransformPosition(P); };

            for (int32 i=1;i<Count;++i)
            {
                const FVector ParentSimPosWS = ToWorld(Chain.CurrPositions[i-1]);
                const FVector ChildSimPosWS  = ToWorld(Chain.CurrPositions[i]);
                Output.AnimInstanceProxy->AnimDrawDebugLine(ParentSimPosWS, ChildSimPosWS, ChainColor, false, 0.f, 2.f, SDPG_World);
                if (DrawLevel > 1)
                {
                    const FVector ParentAnimWS = ToWorld(CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i-1]).GetLocation());
                    const FVector ChildAnimWS  = ToWorld(CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i]).GetLocation());
                    Output.AnimInstanceProxy->AnimDrawDebugLine(ParentAnimWS, ChildAnimWS, FColor::Orange, false, 0.f, 1.f, SDPG_World);
                }
            }
            // Colliders: only at DrawLevel >= 2
            if (DrawLevel > 1)
            {
                const FColor ColColor = FColor::Red;
                if (USkeletalMeshComponent* C = SkelComp)
                {
                    for (int32 c=0;c<ColliderSpherePositionsCS.Num();++c)
                    {
                        const FVector PosWS = C->GetComponentToWorld().TransformPosition(ColliderSpherePositionsCS[c]);
                        Output.AnimInstanceProxy->AnimDrawDebugSphere(PosWS, ColliderSphereRadii[c], 8, ColColor, false, 0.f, SDPG_World);
                    }
                    for (const FCapsuleTmp& Cap : ColliderCapsulesCS)
                    {
                        const FVector AWS = C->GetComponentToWorld().TransformPosition(Cap.A);
                        const FVector BWS = C->GetComponentToWorld().TransformPosition(Cap.B);
                        // Axis line
                        Output.AnimInstanceProxy->AnimDrawDebugLine(AWS, BWS, ColColor, false, 0.f, 1.f, SDPG_World);
                        // End spheres to better visualize the capsule volume
                        Output.AnimInstanceProxy->AnimDrawDebugSphere(AWS, Cap.R, 8, ColColor, false, 0.f, SDPG_World);
                        Output.AnimInstanceProxy->AnimDrawDebugSphere(BWS, Cap.R, 8, ColColor, false, 0.f, SDPG_World);
                    }
                }
            }
        }

        // Write back rotations
        FCompactPose& Pose = Output.Pose;
        for (int32 i=1;i<Count;++i)
        {
            const FVector ParentSimPos = Chain.CurrPositions[i-1]; const FVector ChildSimPos  = Chain.CurrPositions[i];
            const FVector SimDir = (ChildSimPos - ParentSimPos).GetSafeNormal(); if (SimDir.IsNearlyZero()) continue;
            const FVector RefDir = Chain.RestDirections.IsValidIndex(i)? Chain.RestDirections[i] : SimDir; if (RefDir.IsNearlyZero()) continue;

            // Compute a rotation in component-space that aligns the rest direction to simulated direction.
            const FQuat DeltaRotCS = FQuat::FindBetweenNormals(RefDir, SimDir);

            const FCompactPoseBoneIndex ParentIdx = Chain.CompactIndices[i-1];
            const FCompactPoseBoneIndex ChildIdx = Chain.CompactIndices[i];

            // Get current component-space transforms
            const FTransform ParentCSTrans = CSPose.GetComponentSpaceTransform(ParentIdx);
            const FTransform ChildCSTrans = CSPose.GetComponentSpaceTransform(ChildIdx);

            // Apply delta in component space to the child's component rotation
            const FQuat NewChildCSRot = (DeltaRotCS * ChildCSTrans.GetRotation()).GetNormalized();
            FTransform NewChildCSTrans = ChildCSTrans; NewChildCSTrans.SetRotation(NewChildCSRot);

            // Convert back to local (relative to parent) taking scale/rotation into account
            FTransform NewLocal = NewChildCSTrans.GetRelativeTransform(ParentCSTrans);

            FTransform LocalBone = Pose[ChildIdx];
            LocalBone.SetRotation(NewLocal.GetRotation().GetNormalized());
            Pose[ChildIdx] = LocalBone;
        }

        if (DebugLevel >= 2)
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring] Simulated chain: Joints=%d Steps=%d Stiff=%.2f Damp=%.2f"), Count, Steps, Stiff, Damp);
        }
    }
}

void FAnimNode_VRMSpringBone::GatherDebugData(FNodeDebugData& DebugData)
{
    int32 TotalJoints=0, Missing=0; for (const FChainInfo& C : Chains){ TotalJoints += C.CompactIndices.Num(); Missing += C.MissingJointCount; }
    const TCHAR* SimText = bEnableSimulation? TEXT("On") : TEXT("Off");
    const TCHAR* CfgName = SpringConfig ? *SpringConfig->GetName() : TEXT("<null>");
    FString Line = FString::Printf(TEXT("VRMSpringBone (Springs=%d Resolved=%d Missing=%d Sim=%s Steps=%d Dt=%.3f Config=%s)"), Chains.Num(), TotalJoints, Missing, SimText, MaxSubsteps, LastDeltaTime, CfgName);
    DebugData.AddDebugItem(Line);
    ComponentPose.GatherDebugData(DebugData);
}

bool FAnimNode_VRMSpringBone::NeedsRebuild() const
{
    // If no config is set, ensure we clear any existing chains
    if (!SpringConfig) { return Chains.Num() != 0; }
    // Rebuild if the source changed (hash mismatch) or if we haven't built any chains yet.
    return CachedSourceHash != SpringConfig->SourceHash || Chains.Num() == 0;
}

bool FAnimNode_VRMSpringBone::RuntimeNeedsRebuild() const
{
    // When config is removed at runtime, clear existing chains
    if (!SpringConfig) { return Chains.Num() != 0; }
    // Only rebuild when the underlying data changes; don't compare counts since some springs may be invalid/filtered.
    return (CachedSourceHash != SpringConfig->SourceHash);
}

void FAnimNode_VRMSpringBone::BuildChains(const FBoneContainer& BoneContainer)
{
    Chains.Reset();
    const int32 DebugLevel = CVarVRMSpringDebug.GetValueOnAnyThread();

    if (!SpringConfig)
    {
        if (DebugLevel >= 1)
        {
            UE_LOG(LogVRMSpring, Warning,
                TEXT("[VRMSpring] BuildChains: SpringConfig is null; cannot build."));
        }
        return;
    }
    if (!SpringConfig->SpringConfig.IsValid())
    {
        if (DebugLevel >= 1)
        {
            UE_LOG(LogVRMSpring, Warning,
                TEXT("[VRMSpring] BuildChains: SpringConfig->SpringConfig is invalid; cannot build."));
        }
        return;
    }

    const FVRMSpringConfig& Config = SpringConfig->SpringConfig; Chains.Reserve(Config.Springs.Num());
    int32 AddedChains = 0;
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

        // Initialize JointHitRadii to match joint count
        Chain.JointHitRadii.Reset();
        Chain.JointHitRadii.AddDefaulted(Spring.JointIndices.Num());

        for (int32 idx = 0; idx < Spring.JointIndices.Num(); ++idx)
        {
            int32 JointIdx = Spring.JointIndices[idx];
            if (!Config.Joints.IsValidIndex(JointIdx)) { ++Chain.MissingJointCount; continue; }
            const FVRMSpringJoint& J = Config.Joints[JointIdx]; if (J.BoneName.IsNone()) { ++Chain.MissingJointCount; continue; }
            FBoneReference Ref; Ref.BoneName = J.BoneName; Ref.Initialize(BoneContainer);
            if (Ref.IsValidToEvaluate(BoneContainer)) { Chain.CompactIndices.Add(Ref.GetCompactPoseIndex(BoneContainer)); Chain.BoneRefs.Add(MoveTemp(Ref)); Chain.JointHitRadii[idx] = J.HitRadius; }
            else { ++Chain.MissingJointCount; }
        }
        if (Chain.CompactIndices.Num() > 0) { Chains.Add(MoveTemp(Chain)); ++AddedChains; }
    }
    if (SpringConfig) { CachedSourceHash = SpringConfig->SourceHash; }

    if (DebugLevel >= 1)
    {
        int32 TotalMissing=0; for (const FChainInfo& C : Chains){ TotalMissing += C.MissingJointCount; }
        UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring] BuildChains: Added=%d Springs=%d MissingJoints=%d Hash=%s"), AddedChains, Chains.Num(), TotalMissing, *CachedSourceHash);
        for (int32 Idx=0; DebugLevel>=2 && Idx<Chains.Num(); ++Idx)
        {
            const FChainInfo& C = Chains[Idx];
            UE_LOG(LogVRMSpring, Verbose, TEXT("  Chain[%d]: Joints=%d Missing=%d Stiff=%.2f Drag=%.2f Center=%s"), Idx, C.CompactIndices.Num(), C.MissingJointCount, C.SpringStiffness, C.SpringDrag, *C.CenterBoneName.ToString());
        }
    }
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

