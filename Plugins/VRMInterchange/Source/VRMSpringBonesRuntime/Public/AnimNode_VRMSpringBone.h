#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "VRMSpringBoneData.h"
#include "AnimNode_VRMSpringBone.generated.h"

// Pass-through placeholder anim node evolving towards simulation
USTRUCT(BlueprintInternalUseOnly)
struct VRMSPRINGBONESRUNTIME_API FAnimNode_VRMSpringBone : public FAnimNode_Base
{
    GENERATED_BODY()
public:
    // Input pose (component space)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Links")
    FPoseLink ComponentPose;

    // Spring configuration data asset supplied via an ABP variable or directly.
    // Marked Bindable so animation blueprint property binding system can target it.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(PinShownByDefault, Bindable, DisplayName="Spring Config", ToolTip="VRM spring bone configuration data asset (bindable)"))
    TObjectPtr<UVRMSpringBoneData> SpringConfig = nullptr;

    // Simulation toggle
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring")
    bool bEnableSimulation = false; // disabled until simulation phase resumes

    // Global tuning (temporary until per-spring params are wired)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(ClampMin="0", ClampMax="1"))
    float GlobalStiffness = 0.25f; // reserved for future

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(ClampMin="0", ClampMax="2"))
    float GlobalDamping = 0.1f;   // reserved for future

    // Base gravity in component space (cm/s^2). Default UE gravity along -Z.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring")
    FVector Gravity = FVector(0.f, 0.f, -980.f);

    // Optional center pull (per-spring can override via CenterBoneName); 0 disables
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(ClampMin="0", ClampMax="10"))
    float GlobalCenterPullStrength = 0.f;

    // Sub-stepping control
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(ClampMin="1"))
    int32 MaxSubsteps = 2;

    // Target maximum substep dt (seconds). We will split LastDeltaTime to keep each step <= this.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(ClampMin="0.001", ClampMax="0.033"))
    float MaxSubstepDeltaTime = 1.f/90.f;

    // Teleport / reset handling
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring")
    bool bResetOnTeleport = true;

    // If delta time exceeds this threshold (s), we reset simulation instead of integrating.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(ClampMin="0.01", ClampMax="0.5"))
    float TeleportResetTime = 0.2f;

    // External one-shot reset trigger (clears itself after applying)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring")
    bool bForceReset = false;

    // Unit conversion for VRM radii (VRM is meters, UE is centimeters)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision")
    bool bVRMRadiiInMeters = true;

    // Scale applied to VRM radii values when bVRMRadiiInMeters is true (default 100 cm/m)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(EditCondition="bVRMRadiiInMeters", ClampMin="0.1", ClampMax="1000"))
    float VRMToCentimetersScale = 100.f;

    // Collision response tuning
    // Tangential velocity retained after collision: 0 = no friction (retain full tangent), 1 = full stop of tangent
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(ClampMin="0", ClampMax="1"))
    float CollisionFriction = 0.2f;

    // Elasticity along collision normal: 0 = absorb (no bounce), 1 = reflect full normal velocity
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Collision", meta=(ClampMin="0", ClampMax="1"))
    float CollisionRestitution = 0.0f;

    struct FChainInfo
    {
            // Resolved bones
            TArray<FBoneReference> BoneRefs;
            TArray<FCompactPoseBoneIndex> CompactIndices;
            // Rest data
            TArray<float>   SegmentRestLengths;           // per segment rest length (0 for root)
            TArray<FVector> RestDirections;               // rest direction parent->child (0 for root)
            TArray<FVector> RestComponentPositions;       // absolute component positions (ref pose) per joint
            // Runtime (simulation)
            TArray<FVector> CurrPositions;                // simulated/component positions
            TArray<FVector> PrevPositions;                // previous positions
            bool bInitializedPositions = false;
            // Per-spring flattened params
            int32  SourceSpringIndex = INDEX_NONE;
            float  SpringStiffness = 1.f;                 // [0..1]
            float  SpringDrag      = 0.f;                 // [0..1]
            FVector SpringGravity  = FVector::ZeroVector; // cm/s^2 contribution
            float  HitRadius       = 0.f;                 // future collisions
            FName  CenterBoneName;                        // optional
            FCompactPoseBoneIndex CenterCompactIndex = FCompactPoseBoneIndex(INDEX_NONE);
            float  CenterPullStrength = 0.f;              // 0 disables
            // Per-joint radii (from joint entries) — matches CompactIndices ordering
            TArray<float> JointHitRadii;
            // Stats
            int32 IntendedJointCount = 0;
            int32 MissingJointCount = 0;
            void ResetRuntimeState(){ CurrPositions.Reset(); PrevPositions.Reset(); bInitializedPositions=false; }
    };

    TArray<FChainInfo> Chains;
    FString CachedSourceHash; // hash from data asset when last built

    // FAnimNode_Base
    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;

    // Test helper: compute rest data for a chain from a supplied reference skeleton (no BoneContainer needed)
    static void ComputeRestFromReferenceSkeleton(const struct FReferenceSkeleton& RefSkel, FChainInfo& Chain);
    static void ComputeRestFromPositions(FChainInfo& Chain);

    // Lightweight math helpers exposed for tests
    static FVector ProjectPointOnSegment(const FVector& A, const FVector& B, const FVector& P);
    static void ResolveCapsuleCollisionTestHook(FVector& JointPos, FVector& PrevPos, const FVector& A, const FVector& B, float CapsuleRadius, float JointRadius, float Friction, float Restitution);

private:
    void BuildChains(const FBoneContainer& BoneContainer);
    void ComputeReferencePoseRestLengths(const FBoneContainer& BoneContainer); // rest lengths from ref pose

    bool NeedsRebuild() const; // cache-bones time check
    bool RuntimeNeedsRebuild() const; // lightweight check used in Update for dynamic changes

    float LastDeltaTime = 0.f; // retained for future use
    bool bPendingTeleportReset = false;
};
