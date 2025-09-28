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

// Swing-only quaternion (no twist) aligning From to To
static FQuat MakeSwingQuat(const FVector& From, const FVector& To)
{
    FVector A = From.GetSafeNormal();
    FVector B = To.GetSafeNormal();
    if (A.IsNearlyZero() || B.IsNearlyZero())
    {
        return FQuat::Identity;
    }
    float Dot = FMath::Clamp(FVector::DotProduct(A, B), -1.f, 1.f);
    if (Dot > 0.9999f) return FQuat::Identity; // almost aligned
    FVector Axis = FVector::CrossProduct(A, B);
    if (Axis.SizeSquared() < 1e-8f)
    {
        // 180 degree flip; build arbitrary orthogonal axis
        FVector Up = FMath::Abs(A.Z) < 0.99f ? FVector::UpVector : FVector::RightVector;
        Axis = FVector::CrossProduct(A, Up).GetSafeNormal();
    }
    else
    {
        Axis.Normalize();
    }
    const float Angle = FMath::Acos(Dot);
    return FQuat(Axis, Angle);
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

    if (Chains.Num() == 0)
    {
        if (SpringConfig && SpringConfig->SpringConfig.IsValid())
        {
            if (DebugLevel >= 1)
            {
                UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring] Evaluate: Chains empty, building on demand."));
            }
            BuildChains(Output.Pose.GetBoneContainer());
            ComputeReferencePoseRestLengths(Output.Pose.GetBoneContainer());
        }
        else if (DebugLevel >= 1)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMSpring] Evaluate: No chains and config invalid."));
        }
    }

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

    FCSPose<FCompactPose> CSPose;
    CSPose.InitPose(Output.Pose);

    // Collider pre-pass (unchanged from previous version) kept minimal (no filtering logic re-added for brevity)
    TArray<FVector> ColliderSpherePositionsCS; TArray<float> ColliderSphereRadii; struct FCapsuleTmp { FVector A; FVector B; float R; }; TArray<FCapsuleTmp> ColliderCapsulesCS;
    USkeletalMeshComponent* SkelComp = Output.AnimInstanceProxy ? Output.AnimInstanceProxy->GetSkelMeshComponent() : nullptr;
    const float UnitScale = (bVRMRadiiInMeters ? VRMToCentimetersScale : 1.f);
    if (SpringConfig && SpringConfig->SpringConfig.IsValid())
    {
        const FVRMSpringConfig& Config = SpringConfig->SpringConfig;
        for (int32 CIdx=0; CIdx<Config.Colliders.Num(); ++CIdx)
        {
            const FVRMSpringCollider& C = Config.Colliders[CIdx];
            FTransform ColliderBoneCS = FTransform::Identity; bool bHaveBone=false;
            if (!C.BoneName.IsNone() && SkelComp)
            {
                int32 BoneIndex = SkelComp->GetBoneIndex(C.BoneName);
                if (BoneIndex != INDEX_NONE)
                {
                    const FTransform BoneWS = SkelComp->GetBoneTransform(BoneIndex);
                    const FTransform C2W = SkelComp->GetComponentToWorld();
                    ColliderBoneCS = BoneWS.GetRelativeTransform(C2W);
                    bHaveBone=true;
                }
            }
            for (const FVRMSpringColliderSphere& S : C.Spheres)
            {
                FVector P = bHaveBone ? ColliderBoneCS.TransformPosition(S.Offset * UnitScale) : (S.Offset * UnitScale);
                ColliderSpherePositionsCS.Add(P);
                ColliderSphereRadii.Add(FMath::Max(S.Radius * UnitScale, KINDA_SMALL_NUMBER));
            }
            for (const FVRMSpringColliderCapsule& Cap : C.Capsules)
            {
                FCapsuleTmp Tmp; Tmp.A = bHaveBone ? ColliderBoneCS.TransformPosition(Cap.Offset * UnitScale) : (Cap.Offset * UnitScale); Tmp.B = bHaveBone ? ColliderBoneCS.TransformPosition(Cap.TailOffset * UnitScale) : (Cap.TailOffset * UnitScale); Tmp.R = FMath::Max(Cap.Radius * UnitScale, KINDA_SMALL_NUMBER); ColliderCapsulesCS.Add(Tmp);
            }
        }
    }

    for (FChainInfo& Chain : Chains)
    {
        const int32 Count = Chain.CompactIndices.Num(); if (Count==0) continue;

        if (!Chain.bInitializedPositions || bPendingTeleportReset)
        {
            Chain.CurrPositions.SetNum(Count); Chain.PrevPositions.SetNum(Count);
            for (int32 i=0;i<Count;++i)
            {
                Chain.CurrPositions[i] = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i]).GetLocation();
                Chain.PrevPositions[i] = Chain.CurrPositions[i];
            }
            UpdateChainRestFromCSPose(Chain, CSPose);
            Chain.bInitializedPositions = true; bPendingTeleportReset=false;
            continue;
        }

        const float Stiff = FMath::Clamp(Chain.SpringStiffness,0.f,1.f);
        const float Damp  = FMath::Clamp(1.f - FMath::Max(Chain.SpringDrag,0.f),0.01f,1.f);
        const FVector Grav = Gravity + Chain.SpringGravity;

        // Refresh runtime stiffness from config each frame (handles editor tweaks without rebuild)
        if (SpringConfig && SpringConfig->SpringConfig.IsValid())
        {
            const FVRMSpringConfig& RCfg = SpringConfig->SpringConfig;
            if (RCfg.Springs.IsValidIndex(Chain.SourceSpringIndex))
            {
                const FVRMSpring& SpringDef = RCfg.Springs[Chain.SourceSpringIndex];
                float NewStiff = SpringDef.Stiffness > 0.f ? SpringDef.Stiffness : GlobalStiffness;
                if (!FMath::IsNearlyEqual(NewStiff, Chain.SpringStiffness, 1e-4f))
                {
                    Chain.SpringStiffness = FMath::Clamp(NewStiff, 0.f, 1.f);
                }
            }
        }
        const float EffectiveStiff = FMath::Clamp(Chain.SpringStiffness,0.f,1.f);

        // Pin root
        Chain.CurrPositions[0] = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[0]).GetLocation();
        Chain.PrevPositions[0] = Chain.CurrPositions[0];

        float Remaining = FMath::Min(LastDeltaTime, 0.1f);
        const float StepTarget = FMath::Max(0.0001f, MaxSubstepDeltaTime);
        int32 Steps=0;
        while (Remaining > KINDA_SMALL_NUMBER && Steps < MaxSubsteps)
        {
            const float Dt = FMath::Min(Remaining, StepTarget); Remaining -= Dt; ++Steps;

            for (int32 i=1;i<Count;++i)
            {
                const FVector Animated = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i]).GetLocation();
                
                // Handle full stiffness case
                if (EffectiveStiff >= 0.999f)
                {
                    Chain.CurrPositions[i] = Animated;
                    Chain.PrevPositions[i] = Animated;
                    continue;
                }

                const FVector Cur  = Chain.CurrPositions[i];
                const FVector Prev = Chain.PrevPositions[i];
                
                // Calculate velocity based on position difference
                FVector Vel = (Cur - Prev);
                
                // Apply damping to velocity
                Vel *= Damp;
                
                // Apply gravity
                const FVector GravityVel = Grav * Dt;
                Vel += GravityVel;
                
                // Calculate new position from velocity (pure physics)
                FVector PhysicsPos = Cur + Vel * Dt;
                
                // Apply spring force towards animated position
                FVector NewPos = PhysicsPos;
                if (EffectiveStiff > 0.f)
                {
                    // Use exponential decay for smooth, frame-rate independent spring behavior
                    // Higher stiffness = faster convergence to animated position
                    const float SpringStrength = 1.0f - FMath::Exp(-EffectiveStiff * 8.0f * Dt);
                    NewPos = FMath::Lerp(PhysicsPos, Animated, SpringStrength);
                }
                
                // Store for next iteration
                Chain.PrevPositions[i] = Cur;
                Chain.CurrPositions[i] = NewPos;
            }

            // ALWAYS enforce segment rest lengths to prevent ANY stretching
            for (int32 i=1;i<Count;++i)
            {
                const float RestLen = (i < Chain.SegmentRestLengths.Num())? Chain.SegmentRestLengths[i]:0.f; 
                if (RestLen <= KINDA_SMALL_NUMBER) continue;
                
                FVector& ParentP = Chain.CurrPositions[i-1];
                FVector& ChildP  = Chain.CurrPositions[i];
                FVector Delta = ChildP - ParentP; 
                float L = Delta.Size();
                
                if (L <= KINDA_SMALL_NUMBER)
                {
                    // If bone collapsed, restore it using rest direction
                    const FVector Dir = (Chain.RestDirections.IsValidIndex(i) && !Chain.RestDirections[i].IsNearlyZero()) ? 
                                      Chain.RestDirections[i] : FVector::ForwardVector;
                    ChildP = ParentP + Dir * RestLen;
                }
                else
                {
                    // Strictly enforce rest length - no stretching allowed
                    ChildP = ParentP + Delta * (RestLen / L);
                }
            }
        }

        // Collisions
        if (ColliderSpherePositionsCS.Num() || ColliderCapsulesCS.Num())
        {
            for (int32 j=1;j<Count;++j)
            {
                float JointR = (Chain.JointHitRadii.IsValidIndex(j)? Chain.JointHitRadii[j] : Chain.HitRadius) * UnitScale;
                for (int32 s=0;s<ColliderSpherePositionsCS.Num();++s)
                    ResolveSphereCollision(Chain.CurrPositions[j], Chain.PrevPositions[j], ColliderSpherePositionsCS[s], ColliderSphereRadii[s], JointR, CollisionFriction, CollisionRestitution);
                for (const FCapsuleTmp& Cap : ColliderCapsulesCS)
                    ResolveCapsuleCollision(Chain.CurrPositions[j], Chain.PrevPositions[j], Cap.A, Cap.B, Cap.R, JointR, CollisionFriction, CollisionRestitution);
            }
            
            // Re-enforce segment lengths after collision resolution to ensure no stretching
            for (int32 i=1;i<Count;++i)
            {
                const float RestLen = (i < Chain.SegmentRestLengths.Num())? Chain.SegmentRestLengths[i]:0.f; 
                if (RestLen <= KINDA_SMALL_NUMBER) continue;
                
                FVector& ParentP = Chain.CurrPositions[i-1];
                FVector& ChildP  = Chain.CurrPositions[i];
                FVector Delta = ChildP - ParentP; 
                float L = Delta.Size();
                
                if (L <= KINDA_SMALL_NUMBER)
                {
                    const FVector Dir = (Chain.RestDirections.IsValidIndex(i) && !Chain.RestDirections[i].IsNearlyZero()) ? 
                                      Chain.RestDirections[i] : FVector::ForwardVector;
                    ChildP = ParentP + Dir * RestLen;
                }
                else
                {
                    // Strictly enforce rest length - no stretching allowed
                    ChildP = ParentP + Delta * (RestLen / L);
                }
            }
        }
        // Debug draw
        if (DrawLevel>0 && Output.AnimInstanceProxy && SkelComp)
        {
            const FTransform C2W = SkelComp->GetComponentToWorld();
            auto W = [&](const FVector& P){ return C2W.TransformPosition(P); };
            for (int32 i=1;i<Count;++i)
            {
                Output.AnimInstanceProxy->AnimDrawDebugLine(W(Chain.CurrPositions[i-1]), W(Chain.CurrPositions[i]), FColor::Cyan,false,0.f);
                if (DrawLevel>1)
                {
                    const FVector AnimP = W(CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i-1]).GetLocation());
                    const FVector AnimC = W(CSPose.GetComponentSpaceTransform(Chain.CompactIndices[i]).GetLocation());
                    Output.AnimInstanceProxy->AnimDrawDebugLine(AnimP,AnimC,FColor::Orange,false,0.f);
                }
            }
        }

        // Rotation write-back (cumulative swing so children follow bend)
        {
            FCompactPose& PoseRef = Output.Pose;
            if (Count >= 1)
            {
                // 1. Cache animated component-space transforms (baseline)
                TArray<FTransform> AnimCS;
                AnimCS.SetNum(Count);
                for (int32 b = 0; b < Count; ++b)
                {
                    AnimCS[b] = CSPose.GetComponentSpaceTransform(Chain.CompactIndices[b]);
                }

                const FBoneContainer& BC = PoseRef.GetBoneContainer();

                // Parent CS of chain root
                FTransform RootParentCS = FTransform::Identity;
                {
                    const FCompactPoseBoneIndex RootCP = Chain.CompactIndices[0];
                    const FCompactPoseBoneIndex Up = BC.GetParentBoneIndex(RootCP);
                    if (Up != FCompactPoseBoneIndex(INDEX_NONE))
                    {
                        RootParentCS = CSPose.GetComponentSpaceTransform(Up);
                    }
                }

                // 2. Start final CS transforms with simulated positions + animated rotations
                TArray<FTransform> FinalCS;
                FinalCS.SetNum(Count);
                for (int32 i = 0; i < Count; ++i)
                {
                    FinalCS[i] = AnimCS[i];
                    if (Chain.CurrPositions.IsValidIndex(i))
                    {
                        FinalCS[i].SetLocation(Chain.CurrPositions[i]);
                    }
                }

                auto GetRefDir = [&](int32 segParent)->FVector
                {
                    if (segParent < 0 || segParent >= Count - 1)
                        return FVector::ForwardVector;

                    // Prefer stored rest direction (already normalized) if valid
                    if (Chain.RestDirections.IsValidIndex(segParent + 1) &&
                        !Chain.RestDirections[segParent + 1].IsNearlyZero())
                    {
                        return Chain.RestDirections[segParent + 1];
                    }

                    FVector Dir = (AnimCS[segParent + 1].GetLocation() - AnimCS[segParent].GetLocation());
                    if (!Dir.Normalize())
                        Dir = FVector::ForwardVector;
                    return Dir;
                };

                // 3. Precompute segment swings (parent->child)
                const int32 NumSegments = Count - 1;
                TArray<FQuat> SegmentSwing;
                SegmentSwing.SetNum(NumSegments);
                for (int32 s = 0; s < NumSegments; ++s)
                {
                    const FVector SimDir =
                        (FinalCS[s + 1].GetLocation() - FinalCS[s].GetLocation()).GetSafeNormal();
                    if (SimDir.IsNearlyZero())
                    {
                        SegmentSwing[s] = FQuat::Identity;
                        continue;
                    }
                    const FVector RefDir = GetRefDir(s);
                    SegmentSwing[s] = MakeSwingQuat(RefDir, SimDir);
                }

                // 4. Accumulate swing down the chain so each child inherits *all* upstream bending
                FQuat Accum = FQuat::Identity;
                for (int32 i = 0; i < NumSegments; ++i)
                {
                    // Apply current accumulated swing to bone i
                    {
                        const FQuat NewRot = (Accum * AnimCS[i].GetRotation()).GetNormalized();
                        FinalCS[i].SetRotation(NewRot);
                    }

                    // Advance accumulated swing with this segment's bend (left-multiply: world-space delta)
                    if (SegmentSwing[i].IsNormalized())
                    {
                        Accum = (SegmentSwing[i] * Accum).GetNormalized();
                    }
                }

                // 5. Leaf: apply full accumulated bend (so it follows chain). Optionally add its own segment swing,
                // but that would require a terminal aim target; we keep consistent with accumulated upstream bend.
                {
                    const int32 Leaf = Count - 1;
                    const FQuat NewRot = (Accum * AnimCS[Leaf].GetRotation()).GetNormalized();
                    FinalCS[Leaf].SetRotation(NewRot);
                }

                // (Optional twist preservation could analyze original vs new child direction and re-add twist.)

                // 6. Write local-space transforms back into pose
                for (int32 i = 0; i < Count; ++i)
                {
                    const FTransform& ParentCS = (i == 0) ? RootParentCS : FinalCS[i - 1];
                    const FTransform Local = FinalCS[i].GetRelativeTransform(ParentCS);

                    FTransform LocalPose = PoseRef[Chain.CompactIndices[i]];
                    LocalPose.SetRotation(Local.GetRotation().GetNormalized());
                    LocalPose.SetTranslation(Local.GetTranslation());
                    // Preserve scale as authored
                    PoseRef[Chain.CompactIndices[i]] = LocalPose;
                }
            }
        }
    }
}

void FAnimNode_VRMSpringBone::GatherDebugData(FNodeDebugData& DebugData)
{
    int32 TotalJoints=0, Missing=0; for (const FChainInfo& C : Chains){ TotalJoints += C.CompactIndices.Num(); Missing += C.MissingJointCount; }
    FString Line = FString::Printf(TEXT("VRMSpringBone (Springs=%d Joints=%d Missing=%d Dt=%.3f)"), Chains.Num(), TotalJoints, Missing, LastDeltaTime);
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

