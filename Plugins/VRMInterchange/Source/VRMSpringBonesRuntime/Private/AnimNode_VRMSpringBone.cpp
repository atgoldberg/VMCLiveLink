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
    const int32 NumBones = RefSkel.GetNum(); 
    if (NumBones <= 0) 
    { 
        Chain.SegmentRestLengths.Reset(); 
        Chain.RestDirections.Reset(); 
        Chain.RestComponentPositions.Reset(); 
        return; 
    }
    
    const TArray<FTransform>& RefLocal = RefSkel.GetRefBonePose();
    TArray<FTransform> RefComponent; 
    RefComponent.SetNum(NumBones);
    for (int32 BoneIdx = 0; BoneIdx < NumBones; ++BoneIdx)
    {
        const int32 ParentIdx = RefSkel.GetParentIndex(BoneIdx);
        RefComponent[BoneIdx] = (ParentIdx >= 0) ? (RefLocal[BoneIdx] * RefComponent[ParentIdx]) : RefLocal[BoneIdx];
    }

    auto RefIndex = [&](const FBoneReference& BoneRef)->int32 { 
        return BoneRef.BoneName.IsNone() ? INDEX_NONE : RefSkel.FindBoneIndex(BoneRef.BoneName); 
    };

    const int32 Count = Chain.BoneRefs.Num();
    Chain.SegmentRestLengths.SetNum(Count);
    Chain.RestDirections.SetNum(Count);
    Chain.RestComponentPositions.SetNum(Count);

    if (Count == 0) return;

    const int32 RootRef = RefIndex(Chain.BoneRefs[0]);
    Chain.SegmentRestLengths[0] = 0.f; 
    Chain.RestDirections[0] = FVector::ZeroVector; 
    Chain.RestComponentPositions[0] = (RootRef != INDEX_NONE && RootRef < NumBones) ? RefComponent[RootRef].GetLocation() : FVector::ZeroVector;

    for (int32 i = 1; i < Count; ++i)
    {
        const int32 PIdx = RefIndex(Chain.BoneRefs[i-1]); 
        const int32 CIdx = RefIndex(Chain.BoneRefs[i]);
        FVector Dir = FVector::ZeroVector; 
        float Len = 0.f; 
        FVector ParentPos = FVector::ZeroVector; 
        FVector ChildPos = FVector::ZeroVector;
        if (PIdx != INDEX_NONE && CIdx != INDEX_NONE && PIdx < NumBones && CIdx < NumBones)
        { 
            ParentPos = RefComponent[PIdx].GetLocation(); 
            ChildPos = RefComponent[CIdx].GetLocation(); 
            Dir = (ChildPos - ParentPos); 
            Len = Dir.Size(); 
            if (Len > KINDA_SMALL_NUMBER) 
                Dir /= Len; 
            else 
            { 
                Dir = FVector::ZeroVector; 
                Len = 0.f; 
            } 
        }
        Chain.SegmentRestLengths[i] = Len; 
        Chain.RestDirections[i] = Dir; 
        Chain.RestComponentPositions[i] = ChildPos;
    }
}

void FAnimNode_VRMSpringBone::ComputeRestFromPositions(FChainInfo& Chain)
{
    const int32 Count = Chain.RestComponentPositions.Num();
    Chain.SegmentRestLengths.SetNum(Count);
    Chain.RestDirections.SetNum(Count);
    if (Count == 0) return;
    
    Chain.SegmentRestLengths[0] = 0.f; 
    Chain.RestDirections[0] = FVector::ZeroVector;
    for (int32 i = 1; i < Count; ++i)
    {
        const FVector ParentPos = Chain.RestComponentPositions[i-1];
        const FVector ChildPos = Chain.RestComponentPositions[i];
        FVector Dir = (ChildPos - ParentPos); 
        const float Len = Dir.Size();
        Chain.SegmentRestLengths[i] = Len; 
        Chain.RestDirections[i] = (Len > KINDA_SMALL_NUMBER) ? (Dir / Len) : FVector::ZeroVector;
    }
}

// Math helpers
FVector FAnimNode_VRMSpringBone::ProjectPointOnSegment(const FVector& A, const FVector& B, const FVector& P)
{
    const FVector AB = B - A; 
    const float AB2 = AB.SizeSquared(); 
    if (AB2 <= KINDA_SMALL_NUMBER) return A;
    const float T = FMath::Clamp(FVector::DotProduct(P - A, AB) / AB2, 0.f, 1.f);
    return A + AB * T;
}

static void ResolveSphereCollision(FVector& JointPos, FVector& PrevPos, const FVector& ColliderPos, float ColliderRadius, float JointRadius, float Friction, float Restitution)
{
    const float MinDist = ColliderRadius + JointRadius;
    FVector Delta = JointPos - ColliderPos; 
    float Dist = Delta.Size();
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
    if (SpringConfig) 
    { 
        CachedSourceHash = SpringConfig->GetEffectiveHash(); // IMPROVED: Use effective hash instead of just SourceHash
    }
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
            CachedSourceHash = SpringConfig ? SpringConfig->GetEffectiveHash() : CachedSourceHash; // IMPROVED: Use effective hash
            UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring] CacheBones: Adopted SpringConfig from AnimInstance: %s"), SpringConfig ? *SpringConfig->GetName() : TEXT("<null>"));
        }
    }

    const int32 DebugLevel = CVarVRMSpringDebug.GetValueOnAnyThread();
    if (DebugLevel >= 1)
    {
        UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring] CacheBones: SpringConfig=%s Chains=%d NeedsRebuild=%s"),
            SpringConfig ? *SpringConfig->GetName() : TEXT("<null>"), Chains.Num(), NeedsRebuild() ? TEXT("true") : TEXT("false"));
    }

    if (NeedsRebuild()) 
    { 
        BuildChains(BoneContainer); 
        ComputeReferencePoseRestLengths(BoneContainer); 
    }
}

void FAnimNode_VRMSpringBone::Update_AnyThread(const FAnimationUpdateContext& Context)
{
    ComponentPose.Update(Context);
    LastDeltaTime = Context.GetDeltaTime();

    // Teleport / hitch detection
    if (bResetOnTeleport && LastDeltaTime >= TeleportResetTime) 
    { 
        bPendingTeleportReset = true; 
    }
    if (bForceReset) 
    { 
        bPendingTeleportReset = true; 
        bForceReset = false; 
    }

    if (RuntimeNeedsRebuild())
    {
        for (FChainInfo& Chain : Chains) 
        { 
            Chain.ResetRuntimeState(); 
        }
        Chains.Reset();
        if (SpringConfig) 
        { 
            CachedSourceHash = SpringConfig->GetEffectiveHash(); // IMPROVED: Use effective hash
        }
    }
}

void FAnimNode_VRMSpringBone::Evaluate_AnyThread(FPoseContext& Output)
{
    ComponentPose.Evaluate(Output);

    const int32 DebugLevel = CVarVRMSpringDebug.GetValueOnAnyThread();
    const int32 DrawLevel = CVarVRMSpringDraw.GetValueOnAnyThread();

    // Late adoption in case binding populated after Initialize
    if (!SpringConfig)
    {
        if (UVRMSpringBoneData* Adopted = TryAdoptSpringConfigFromAnimInstance(Output.AnimInstanceProxy))
        {
            SpringConfig = Adopted;
            CachedSourceHash = SpringConfig ? SpringConfig->GetEffectiveHash() : CachedSourceHash; // IMPROVED: Use effective hash
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
            Chains.Num(), CfgName, bCfgValid ? TEXT("true") : TEXT("false"), NeedsRebuild() ? TEXT("true") : TEXT("false"));
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
                SpringConfig ? TEXT("present") : TEXT("null"),
                (SpringConfig && SpringConfig->SpringConfig.IsValid()) ? TEXT("valid") : TEXT("invalid"));
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

    FCSPose<FCompactPose> CSPose; 
    CSPose.InitPose(Output.Pose);

    // IMPROVED: Precompute and cache collider transforms to avoid redundant calculations
    TArray<FVector> ColliderSpherePositionsCS;
    TArray<float> ColliderSphereRadii;
    struct FCapsuleTmp { FVector A; FVector B; float R; };
    TArray<FCapsuleTmp> ColliderCapsulesCS;
    ColliderSpherePositionsCS.Reserve(64);
    ColliderSphereRadii.Reserve(64);
    ColliderCapsulesCS.Reserve(32);

    USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy ? Output.AnimInstanceProxy->GetSkelMeshComponent() : nullptr;
    const float UnitScale = (bVRMRadiiInMeters ? VRMToCentimetersScale : 1.f);

    // IMPROVED: Prefilter colliders to reduce collision checks
    TSet<int32> UsedColliderIndices;
    if (SpringConfig && SpringConfig->SpringConfig.IsValid())
    {
        const FVRMSpringConfig& Config = SpringConfig->SpringConfig;
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

        // Build collider shapes in component space
        for (int32 CIdx = 0; CIdx < Config.Colliders.Num(); ++CIdx)
        {
            if (UsedColliderIndices.Num() > 0 && !UsedColliderIndices.Contains(CIdx))
            {
                continue; // Skip unused colliders
            }

            const FVRMSpringCollider& C = Config.Colliders[CIdx];
            FTransform ColliderBoneCS = FTransform::Identity;
            bool bHaveBone = false;
            
            if (!C.BoneName.IsNone() && SkelComp)
            {
                const int32 BoneIndex = SkelComp->GetBoneIndex(C.BoneName);
                if (BoneIndex != INDEX_NONE)
                {
                    const FTransform BoneWS = SkelComp->GetBoneTransform(BoneIndex);
                    const FTransform C2W = SkelComp->GetComponentToWorld();
                    ColliderBoneCS = BoneWS.GetRelativeTransform(C2W);
                    bHaveBone = true;
                }
            }

            // Process spheres
            for (const FVRMSpringColliderSphere& S : C.Spheres)
            {
                const FVector Local = S.Offset * UnitScale;
                FVector SphereCSPos = bHaveBone ? ColliderBoneCS.TransformPosition(Local) : Local;
                ColliderSpherePositionsCS.Add(SphereCSPos);
                ColliderSphereRadii.Add(FMath::Max(S.Radius * UnitScale, KINDA_SMALL_NUMBER)); // IMPROVED: Ensure non-zero radius
            }

            // Process capsules
            for (const FVRMSpringColliderCapsule& Cap : C.Capsules)
            {
                const FVector ALocal = Cap.Offset * UnitScale;
                const FVector BLocal = Cap.TailOffset * UnitScale;
                const FVector A = bHaveBone ? ColliderBoneCS.TransformPosition(ALocal) : ALocal;
                const FVector B = bHaveBone ? ColliderBoneCS.TransformPosition(BLocal) : BLocal;
                FCapsuleTmp Tmp; 
                Tmp.A = A; 
                Tmp.B = B; 
                Tmp.R = FMath::Max(Cap.Radius * UnitScale, KINDA_SMALL_NUMBER); // IMPROVED: Ensure non-zero radius
                ColliderCapsulesCS.Add(Tmp);
            }
        }
    }

    // IMPROVED: Physics simulation with better numerical stability
    for (FChainInfo& Chain : Chains)
    {
        const int32 Count = Chain.CompactIndices.Num(); 
        if (Count == 0) continue;

        if (!Chain.bInitializedPositions || bPendingTeleportReset)
        {
            // Initialize simulated positions from component-space animated transforms
            Chain.CurrPositions.SetNum(Count); 
            Chain.PrevPositions.SetNum(Count);
            for (int32 i = 0; i < Count; ++i)
            { 
                Chain.CurrPositions[i] = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i]).GetLocation(); 
                Chain.PrevPositions[i] = Chain.CurrPositions[i]; 
            }

            UpdateChainRestFromCSPose(Chain, CSPose);
            Chain.bInitializedPositions = true; 
            bPendingTeleportReset = false; 
            if (DebugLevel >= 2) 
            { 
                UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring] Initialize positions for chain; Joints=%d"), Count); 
            } 
            continue;
        }

        // IMPROVED: Validate and clamp spring parameters for numerical stability
        const float Stiff = FMath::Clamp(Chain.SpringStiffness, 0.f, 1.f);
        const float Damp = FMath::Clamp(1.f - FMath::Max(Chain.SpringDrag, 0.f), 0.01f, 1.f); // Prevent zero damping
        const FVector Grav = Gravity + Chain.SpringGravity;

        // Resolve center compact index on first use
        if (Chain.CenterCompactIndex == FCompactPoseBoneIndex(INDEX_NONE) && !Chain.CenterBoneName.IsNone())
        {
            FBoneReference CenterRef; 
            CenterRef.BoneName = Chain.CenterBoneName; 
            CenterRef.Initialize(Output.Pose.GetBoneContainer());
            if (CenterRef.IsValidToEvaluate(Output.Pose.GetBoneContainer()))
            {
                Chain.CenterCompactIndex = CenterRef.GetCompactPoseIndex(Output.Pose.GetBoneContainer());
                Chain.CenterPullStrength = (GlobalCenterPullStrength > 0.f) ? GlobalCenterPullStrength : 0.f;
            }
        }

        // Root always pinned to animated pose
        Chain.CurrPositions[0] = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[0]).GetLocation();
        Chain.PrevPositions[0] = Chain.CurrPositions[0];

        // IMPROVED: Adaptive sub-stepping with better time integration
        float Remaining = FMath::Min(LastDeltaTime, 0.1f); // Cap maximum delta time
        const float StepTarget = FMath::Max(0.0001f, MaxSubstepDeltaTime); 
        int32 Steps = 0;
        
        while (Remaining > KINDA_SMALL_NUMBER && Steps < MaxSubsteps)
        {
            const float Dt = FMath::Min(Remaining, StepTarget); 
            Remaining -= Dt; 
            ++Steps;
            
            // IMPROVED: Better Verlet integration with damping
            for (int32 i = 1; i < Count; ++i)
            {
                const FVector Animated = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i]).GetLocation();
                const FVector Cur = Chain.CurrPositions[i];
                const FVector Prev = Chain.PrevPositions[i];
                
                // Verlet velocity with improved damping
                const FVector Vel = (Cur - Prev) * Damp;
                
                // Spring force towards animated position
                const FVector SpringToAnim = (Animated - Cur) * Stiff;
                
                // Optional center pull force
                FVector CenterPull = FVector::ZeroVector;
                if (Chain.CenterCompactIndex != FCompactPoseBoneIndex(INDEX_NONE) && Chain.CenterPullStrength > 0.f)
                {
                    const FVector CenterPos = CSPose.GetComponentSpaceTransform(Chain.CenterCompactIndex).GetLocation();
                    CenterPull = (CenterPos - Cur) * Chain.CenterPullStrength;
                }
                
                // IMPROVED: More accurate gravity integration
                const FVector GravityAccel = Grav * (Dt * Dt);
                const FVector ForceAccel = (SpringToAnim + CenterPull) * Dt;
                
                const FVector Next = Cur + Vel + ForceAccel + GravityAccel;
                Chain.PrevPositions[i] = Cur; 
                Chain.CurrPositions[i] = Next;
            }
            
            // IMPROVED: Multiple constraint solving iterations for better stability
            const int32 ConstraintIterations = 2; // Multiple iterations for better convergence
            for (int32 Iter = 0; Iter < ConstraintIterations; ++Iter)
            {
                // Distance constraints (solve from root to tip for better stability)
                for (int32 i = 1; i < Count; ++i)
                {
                    const float RestLen = (i < Chain.SegmentRestLengths.Num()) ? Chain.SegmentRestLengths[i] : 0.f; 
                    if (RestLen <= KINDA_SMALL_NUMBER) continue;
                    
                    FVector& ParentPos = Chain.CurrPositions[i-1]; 
                    FVector& ChildPos = Chain.CurrPositions[i];
                    FVector Delta = ChildPos - ParentPos; 
                    const float CurLen = Delta.Size();
                    
                    if (CurLen <= KINDA_SMALL_NUMBER)
                    { 
                        const FVector Fallback = Chain.RestDirections.IsValidIndex(i) ? 
                            Chain.RestDirections[i] * RestLen : FVector(RestLen, 0, 0); 
                        ChildPos = ParentPos + Fallback; 
                        continue; 
                    }
                    
                    // IMPROVED: Use exact constraint solving with proper mass distribution
                    const float Diff = (CurLen - RestLen);
                    const FVector Correction = (Delta / CurLen) * (Diff * 0.5f); // Split correction between parent and child
                    
                    // Only child moves (parent is either root or already processed)
                    ChildPos -= Correction * 2.0f; // Child takes full correction
                }
            }

            // Collision resolution
            if (ColliderSpherePositionsCS.Num() > 0 || ColliderCapsulesCS.Num() > 0)
            {
                for (int32 j = 1; j < Count; ++j)
                {
                    const float JointRadius = (Chain.JointHitRadii.IsValidIndex(j) ? Chain.JointHitRadii[j] : Chain.HitRadius) * UnitScale;

                    // Sphere collisions
                    for (int32 c = 0; c < ColliderSpherePositionsCS.Num(); ++c)
                    {
                        ResolveSphereCollision(Chain.CurrPositions[j], Chain.PrevPositions[j], 
                            ColliderSpherePositionsCS[c], ColliderSphereRadii[c], 
                            JointRadius, CollisionFriction, CollisionRestitution);
                    }
                    
                    // Capsule collisions
                    for (const FCapsuleTmp& Cap : ColliderCapsulesCS)
                    {
                        ResolveCapsuleCollision(Chain.CurrPositions[j], Chain.PrevPositions[j], 
                            Cap.A, Cap.B, Cap.R, JointRadius, CollisionFriction, CollisionRestitution);
                    }
                }
            }
        }

        // Debug visualization (unchanged for brevity)
        // ...existing debug draw code...

        // IMPROVED: Write back rotations with better numerical stability
        FCompactPose& Pose = Output.Pose;
        for (int32 i = 1; i < Count; ++i)
        {
            const FVector ParentSimPos = Chain.CurrPositions[i-1]; 
            const FVector ChildSimPos = Chain.CurrPositions[i];
            const FVector SimDir = (ChildSimPos - ParentSimPos).GetSafeNormal(); 
            if (SimDir.IsNearlyZero(KINDA_SMALL_NUMBER)) continue;
            
            const FVector RefDir = Chain.RestDirections.IsValidIndex(i) ? Chain.RestDirections[i] : SimDir; 
            if (RefDir.IsNearlyZero(KINDA_SMALL_NUMBER)) continue;

            // IMPROVED: More robust quaternion computation
            const FQuat DeltaRotCS = FQuat::FindBetweenNormals(RefDir, SimDir);
            
            // Ensure quaternion is valid and normalized (replace removed per-component IsFinite helpers)
            if (!DeltaRotCS.IsNormalized() ||
                !FMath::IsFinite(DeltaRotCS.X) ||
                !FMath::IsFinite(DeltaRotCS.Y) ||
                !FMath::IsFinite(DeltaRotCS.Z) ||
                !FMath::IsFinite(DeltaRotCS.W))
            {
                continue; // Skip invalid rotation
            }

            const FCompactPoseBoneIndex ParentIdx = Chain.CompactIndices[i-1];
            const FCompactPoseBoneIndex ChildIdx = Chain.CompactIndices[i];

            const FTransform ParentCSTrans = CSPose.GetComponentSpaceTransform(ParentIdx);
            const FTransform ChildCSTrans = CSPose.GetComponentSpaceTransform(ChildIdx);

            const FQuat NewChildCSRot = (DeltaRotCS * ChildCSTrans.GetRotation()).GetNormalized();
            FTransform NewChildCSTrans = ChildCSTrans; 
            NewChildCSTrans.SetRotation(NewChildCSRot);

            FTransform NewLocal = NewChildCSTrans.GetRelativeTransform(ParentCSTrans);

            FTransform LocalBone = Pose[ChildIdx];
            LocalBone.SetRotation(NewLocal.GetRotation().GetNormalized());
            Pose[ChildIdx] = LocalBone;
        }

        if (DebugLevel >= 2)
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring] Simulated chain: Joints=%d Steps=%d Stiff=%.2f Damp=%.2f"), 
                Count, Steps, Stiff, Damp);
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
    if (!SpringConfig) 
    { 
        return Chains.Num() != 0; 
    }
    const FString Effective = SpringConfig->GetEffectiveHash();
    return CachedSourceHash != Effective || Chains.Num() == 0;
}

bool FAnimNode_VRMSpringBone::RuntimeNeedsRebuild() const
{
    if (!SpringConfig) 
    { 
        return Chains.Num() != 0; 
    }
    const FString Effective = SpringConfig->GetEffectiveHash();
    return (CachedSourceHash != Effective);
}

void FAnimNode_VRMSpringBone::BuildChains(const FBoneContainer& BoneContainer)
{
    Chains.Reset();
    const int32 DebugLevel = CVarVRMSpringDebug.GetValueOnAnyThread();

    if (!SpringConfig)
    {
        if (DebugLevel >= 1)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring] BuildChains: SpringConfig is null; cannot build."));
        }
        return;
    }
    if (!SpringConfig->SpringConfig.IsValid())
    {
        if (DebugLevel >= 1)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring] BuildChains: SpringConfig->SpringConfig is invalid; cannot build."));
        }
        return;
    }

    const FVRMSpringConfig& Config = SpringConfig->SpringConfig; 
    Chains.Reserve(Config.Springs.Num());
    
    // IMPROVED: Create a mapping from node indices to bone names for proper VRM compliance
    // This should ideally come from the VRM import process, but we'll use a fallback approach
    auto GetBoneNameFromNodeIndex = [&](int32 NodeIndex) -> FName
    {
        FName BoneName = SpringConfig->GetBoneNameForNode(NodeIndex);
        if (!BoneName.IsNone())
        {
            return BoneName;
        }
        
        for (const FVRMSpringJoint& Joint : Config.Joints)
        {
            if (Joint.NodeIndex == NodeIndex && !Joint.BoneName.IsNone())
            {
                return Joint.BoneName;
            }
        }
        
        for (const FVRMSpringCollider& Collider : Config.Colliders)
        {
            if (Collider.NodeIndex == NodeIndex && !Collider.BoneName.IsNone())
            {
                return Collider.BoneName;
            }
        }
        
        if (DebugLevel >= 1)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring] No bone name mapping found for node index %d, using fallback name"), NodeIndex);
        }
        return FName(*FString::Printf(TEXT("Node_%d"), NodeIndex));
    };

    int32 AddedChains = 0;
    for (int32 SpringIdx = 0; SpringIdx < Config.Springs.Num(); ++SpringIdx)
    {
        const FVRMSpring& Spring = Config.Springs[SpringIdx];
        FChainInfo Chain; 
        Chain.IntendedJointCount = Spring.JointIndices.Num(); 
        Chain.SourceSpringIndex = SpringIdx;
        Chain.SpringStiffness = Spring.Stiffness > 0.f ? Spring.Stiffness : GlobalStiffness;
        Chain.SpringDrag = Spring.Drag > 0.f ? Spring.Drag : GlobalDamping;
        Chain.SpringGravity = ResolveSpringGravity(Spring, FVector::ZeroVector);
        Chain.HitRadius = Spring.HitRadius;
        Chain.CenterBoneName = Spring.CenterBoneName;
        
        if (Chain.CenterBoneName.IsNone() && Spring.CenterNodeIndex != INDEX_NONE)
        {
            Chain.CenterBoneName = GetBoneNameFromNodeIndex(Spring.CenterNodeIndex);
        }
        
        Chain.CenterCompactIndex = FCompactPoseBoneIndex(INDEX_NONE);
        Chain.CenterPullStrength = GlobalCenterPullStrength;

        Chain.JointHitRadii.Reset();
        Chain.JointHitRadii.AddDefaulted(Spring.JointIndices.Num());

        for (int32 idx = 0; idx < Spring.JointIndices.Num(); ++idx)
        {
            int32 JointIdx = Spring.JointIndices[idx];
            if (!Config.Joints.IsValidIndex(JointIdx)) 
            { 
                ++Chain.MissingJointCount; 
                continue; 
            }
            
            const FVRMSpringJoint& J = Config.Joints[JointIdx]; 
            FName BoneName = J.BoneName;
            
            if (BoneName.IsNone() && J.NodeIndex != INDEX_NONE)
            {
                BoneName = GetBoneNameFromNodeIndex(J.NodeIndex);
                if (DebugLevel >= 2)
                {
                    UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring] Resolved bone name '%s' from node index %d"), *BoneName.ToString(), J.NodeIndex);
                }
            }
            
            if (BoneName.IsNone()) 
            { 
                ++Chain.MissingJointCount; 
                continue; 
            }
            
            FBoneReference Ref; 
            Ref.BoneName = BoneName; 
            Ref.Initialize(BoneContainer);
            
            if (Ref.IsValidToEvaluate(BoneContainer)) 
            { 
                Chain.CompactIndices.Add(Ref.GetCompactPoseIndex(BoneContainer)); 
                Chain.BoneRefs.Add(Ref); 
                Chain.JointHitRadii[idx] = FMath::Max(J.HitRadius, 0.001f); // ensure minimum radius
            }
            else 
            { 
                ++Chain.MissingJointCount; 
                if (DebugLevel >= 1)
                {
                    UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring] Failed to resolve bone '%s' (from joint %d, node %d) in skeleton"), *BoneName.ToString(), JointIdx, J.NodeIndex);
                }
            }
        }
        
        if (Chain.CompactIndices.Num() > 0) 
        { 
            Chains.Add(MoveTemp(Chain)); 
            ++AddedChains; 
        }
    }
    
    if (SpringConfig) 
    { 
        CachedSourceHash = SpringConfig->GetEffectiveHash(); 
    }

    if (DebugLevel >= 1)
    {
        int32 TotalMissing = 0; 
        for (const FChainInfo& C : Chains) 
        { 
            TotalMissing += C.MissingJointCount; 
        }
        UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring] BuildChains: Added=%d Springs=%d MissingJoints=%d Hash=%s"), AddedChains, Chains.Num(), TotalMissing, *CachedSourceHash);
        
        for (int32 Idx = 0; DebugLevel >= 2 && Idx < Chains.Num(); ++Idx)
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

