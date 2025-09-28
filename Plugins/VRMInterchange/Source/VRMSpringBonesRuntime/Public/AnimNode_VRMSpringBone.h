#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimInstanceProxy.h"
#include "VRMSpringBoneData.h"
#include "AnimNode_VRMSpringBone.generated.h"

struct FVRMSBJointCache
{
    FVRMSBJointCache() : BoneIndex(FCompactPoseBoneIndex(INDEX_NONE)) {}
    FCompactPoseBoneIndex BoneIndex; 
    FName BoneName;                  
    float HitRadius = 0.f;
    float RestLength = 0.f;          
    FVector RestDirection = FVector::ZeroVector; 
    FVector PrevTip = FVector::ZeroVector;       
    FVector CurrTip = FVector::ZeroVector;       
    FVector ParentRefPos = FVector::ZeroVector;  
    FQuat InitialLocalRotation = FQuat::Identity;          
    FTransform InitialLocalTransform;                      
    FVector BoneAxisLocal = FVector::ForwardVector;        
    float BoneLengthLocal = 0.f;                           
    bool bValid = false;
};

struct FVRMSBChainCache
{
    int32 SpringIndex = INDEX_NONE;
    FVector GravityDir = FVector(0,0,-1);
    float GravityPower = 0.f;
    float Stiffness = 0.f;
    float Drag = 0.f;
    FCompactPoseBoneIndex CenterBoneIndex = FCompactPoseBoneIndex(INDEX_NONE);
    FName CenterBoneName;
    bool bHasCenter = false; 
    TArray<FVRMSBJointCache> Joints;
    TArray<int32> SphereShapeIndices;
    TArray<int32> CapsuleShapeIndices;
    TArray<int32> PlaneShapeIndices; 
};

struct FVRMSBSphereShapeCache
{
    FCompactPoseBoneIndex BoneIndex;
    FName BoneName;
    FVector LocalOffset = FVector::ZeroVector;
    float Radius = 0.f;
    bool bInside = false; 
    bool bValid = false;
};

struct FVRMSBCapsuleShapeCache
{
    FCompactPoseBoneIndex BoneIndex;
    FName BoneName;
    FVector LocalP0 = FVector::ZeroVector;
    FVector LocalP1 = FVector::ZeroVector;
    float Radius = 0.f;
    bool bInside = false; 
    bool bValid = false;
};

struct FVRMSBPlaneShapeCache
{
    FCompactPoseBoneIndex BoneIndex;
    FName BoneName;
    FVector LocalPoint = FVector::ZeroVector; 
    FVector LocalNormal = FVector(0,0,1);     
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Blend", meta=(ClampMin="0.0", ClampMax="1.0"))
    float Weight = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation")
    bool bUseFixedTimeStep = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation", meta=(EditCondition="bUseFixedTimeStep", ClampMin="0.0001", UIMin="0.001", UIMax="0.033"))
    float FixedTimeStep = 1.f / 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation", meta=(ClampMin="1", ClampMax="32"))
    int32 MaxSubsteps = 8;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation", meta=(ClampMin="0.001", UIMin="0.016", UIMax="0.25"))
    float MaxDeltaTime = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation", meta=(EditCondition="!bUseFixedTimeStep", ClampMin="1", ClampMax="32"))
    int32 VariableSubsteps = 1;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Simulation", meta=(ClampMin="1", ClampMax="4"))
    int32 ConstraintIterations = 2;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Advanced", meta=(ClampMin="0.0", UIMin="0.0", UIMax="5.0"))
    float RotationDeadZoneDeg = 0.5f;

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
    void BuildComponentSpacePose(const FPoseContext& SourcePose, FCSPose<FCompactPose>& OutCSPose) const;
    // Non-const pose references (UE method GetComponentSpaceTransform is non-const)
    void PrepareColliderWorldCaches(FCSPose<FCompactPose>& CSPose);
    void SimulateChains(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& CSPose);
    void ApplyRotations(FPoseContext& Output, FCSPose<FCompactPose>& CSPose);

    TArray<FVRMSBChainCache> ChainCaches;
    TArray<FVRMSBSphereShapeCache> SphereShapeCaches;
    TArray<FVRMSBCapsuleShapeCache> CapsuleShapeCaches;
    TArray<FVRMSBPlaneShapeCache> PlaneShapeCaches;

    FString LastAssetHash;
    TWeakObjectPtr<UVRMSpringBoneData> LastAssetPtr;
    bool bCachesValid = false;

    int32 TotalValidJoints = 0; 
    uint64 LastLoggedFrame = 0;
    uint64 LastStepLoggedFrame = 0; 
    int32 CachedSubsteps = 1;
    float CachedH = 1.0f / 60.0f;
    uint64 LastSimulatedFrame = 0;
    float TimeAccumulator = 0.f;

    TArray<FVector> SphereWorldPos;
    TArray<FVector> CapsuleWorldP0;
    TArray<FVector> CapsuleWorldP1;
    TArray<FVector> PlaneWorldPoint;
    TArray<FVector> PlaneWorldNormal;

    bool bLimitsEnabled = false;
    bool bElasticityEnabled = false; 
    bool bWasWeightActive = true; 
};
