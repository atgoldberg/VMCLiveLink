#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimInstanceProxy.h"
#include "VRMSpringBoneData.h"
#include "AnimNode_VRMSpringBone.generated.h"

struct FVRMSBJointCache
{
    FVRMSBJointCache() : BoneIndex(FCompactPoseBoneIndex(INDEX_NONE)) {}
    FCompactPoseBoneIndex BoneIndex; // compact pose index
    FName BoneName;                  // cached bone name for ref skeleton lookup
    float HitRadius = 0.f;
    float RestLength = 0.f;          // component-space rest length to child
    FVector RestDirection = FVector::ZeroVector; // component-space rest direction to child
    FVector PrevTip = FVector::ZeroVector;       // previous tail position (component space)
    FVector CurrTip = FVector::ZeroVector;       // current tail position (component space)
    FVector ParentRefPos = FVector::ZeroVector;  // reference parent position in component space

    // Added for future rotation write-back & spec compliance (Tasks 2 groundwork)
    FQuat InitialLocalRotation = FQuat::Identity;          // joint's initial local rotation
    FTransform InitialLocalTransform;                     // full initial local transform
    FVector BoneAxisLocal = FVector::ForwardVector;       // direction to child in joint local space
    float BoneLengthLocal = 0.f;                          // length to child in joint local space

    bool bValid = false;
};

struct FVRMSBChainCache
{
    int32 SpringIndex = INDEX_NONE;
    FVector GravityDir = FVector(0,0,-1);
    float GravityPower = 0.f;
    float Stiffness = 0.f;
    float Drag = 0.f;
    TArray<FVRMSBJointCache> Joints;
    TArray<int32> SphereShapeIndices;
    TArray<int32> CapsuleShapeIndices;
    void Reset()
    {
        SpringIndex = INDEX_NONE; GravityDir = FVector(0,0,-1); GravityPower = 0.f; Stiffness = 0.f; Drag = 0.f;
        Joints.Reset(); SphereShapeIndices.Reset(); CapsuleShapeIndices.Reset();
    }
};

struct FVRMSBSphereShapeCache
{
    FCompactPoseBoneIndex BoneIndex;
    FName BoneName;
    FVector LocalOffset = FVector::ZeroVector;
    float Radius = 0.f;
    bool bInside = false; // Task 07: inside/outside semantics from asset
    bool bValid = false;
};

struct FVRMSBCapsuleShapeCache
{
    FCompactPoseBoneIndex BoneIndex;
    FName BoneName;
    FVector LocalP0 = FVector::ZeroVector;
    FVector LocalP1 = FVector::ZeroVector;
    float Radius = 0.f;
    bool bInside = false; // Task 07
    bool bValid = false;
};

USTRUCT(BlueprintType)
struct VRMSPRINGBONESRUNTIME_API FAnimNode_VRMSpringBone : public FAnimNode_Base
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Links")
    FPoseLink ComponentPose;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(PinShownByDefault, DisplayName="Spring Data"))
    UVRMSpringBoneData* SpringConfig = nullptr;

    // --- Task 3: Deterministic Sub-Stepping Controls ---
    // If true, accumulate time and simulate in fixed-size steps (FixedTimeStep) up to MaxSubsteps per frame.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation")
    bool bUseFixedTimeStep = true;

    // Fixed simulation step (seconds) when bUseFixedTimeStep. Typical 1/60.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation", meta=(EditCondition="bUseFixedTimeStep", ClampMin="0.0001", UIMin="0.001", UIMax="0.033"))
    float FixedTimeStep = 1.f / 60.f;

    // Maximum number of fixed substeps processed per frame (prevents spiraling when frame rate is low).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation", meta=(ClampMin="1", ClampMax="32"))
    int32 MaxSubsteps = 8;

    // Clamp excessively large delta times (e.g., hitch frames) to maintain stability.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation", meta=(ClampMin="0.001", UIMin="0.016", UIMax="0.25"))
    float MaxDeltaTime = 0.1f;

    // If not using fixed timestep, this many substeps are used per frame (variable size = DeltaTime / Substeps)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation", meta=(EditCondition="!bUseFixedTimeStep", ClampMin="1", ClampMax="32"))
    int32 VariableSubsteps = 1;

    virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
    virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
    virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
    virtual void Evaluate_AnyThread(FPoseContext& Output) override;
    virtual void GatherDebugData(FNodeDebugData& DebugData) override;
private:
    void InvalidateCaches();
    void RebuildCaches_AnyThread(const FBoneContainer& BoneContainer);

    FCompactPoseBoneIndex ResolveBone(const FBoneContainer& BoneContainer, const FName& BoneName) const;
    FCompactPoseBoneIndex ResolveBoneByNodeIndex(const FBoneContainer& BoneContainer, int32 NodeIndex) const;

    TArray<FVRMSBChainCache> ChainCaches;
    TArray<FVRMSBSphereShapeCache> SphereShapeCaches;
    TArray<FVRMSBCapsuleShapeCache> CapsuleShapeCaches;

    FString LastAssetHash;
    TWeakObjectPtr<UVRMSpringBoneData> LastAssetPtr;
    bool bCachesValid = false;

    int32 TotalValidJoints = 0;

    uint64 LastLoggedFrame = 0;

    // Cached deterministic stepping params produced in Update_AnyThread and consumed in Evaluate_AnyThread
    int32 CachedSubsteps = 1;
    float CachedH = 1.0f / 60.0f;
    uint64 LastSimulatedFrame = 0;

    // Time accumulator for fixed timestep mode (Task 3)
    float TimeAccumulator = 0.f;

    // Task 07: per-frame world-space collider caches (no allocation churn)
    TArray<FVector> SphereWorldPos;      // size == SphereShapeCaches
    TArray<FVector> CapsuleWorldP0;      // size == CapsuleShapeCaches
    TArray<FVector> CapsuleWorldP1;      // size == CapsuleShapeCaches
};
