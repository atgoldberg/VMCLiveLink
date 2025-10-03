#pragma once

#include "CoreMinimal.h"
#include "BoneControllers/AnimNode_SkeletalControlBase.h"
#include "VRMSpringBoneData.h"
#include "AnimNode_VRMSpringBones.generated.h"

// Forward declarations (avoid heavy includes)
struct FVRMSpringConfig;
class UVRMSpringBonesData;

/**
 * Per–joint runtime simulation state (minimal set after dead-code removal).
 */
struct FVRMSimJointState
{
	// Initialization flag
	uint8 bInitialized = 0;

	// Static (per rest pose) data
	FVector BoneAxisLocal = FVector::ForwardVector;
	FVector InitialLocalChildPos = FVector::ZeroVector;
	float   WorldBoneLength = 0.f;

	// Dynamic state
	FVector CurrentTail = FVector::ZeroVector;
	FVector PrevTail    = FVector::ZeroVector;
	FVector PrevHeadCS  = FVector::ZeroVector;
};

/** Range info for a spring chain (indices into the spring's JointIndices array). */
struct FSpringChainRange
{
	int32 First = 0;
	int32 Num   = 0;
};

/** Deferred bone write after simulation. */
struct FBoneWrite
{
	FCompactPoseBoneIndex BoneIndex;
	FVector               NewPosition;
	FQuat                 NewRotation;
};

/**
 * Spring bone solver anim node (VRM multi-chain).
 * Cleaned version without unused/dead code.
 */
USTRUCT(BlueprintInternalUseOnly)
struct VRMSPRINGBONESRUNTIME_API FAnimNode_VRMSpringBones : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

public:
	/** Master enable switch */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(PinShownByDefault))
	bool bEnable = true;

	/** Optional runtime pause (debug) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Debug", meta=(PinShownByDefault))
	bool bPauseSimulation = false;

	/** Spring configuration asset (contains joints, springs, colliders) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring", meta=(PinShownByDefault))
	TObjectPtr<UVRMSpringBoneData> SpringData = nullptr;

	/** Spring configuration asset (contains joints, springs, colliders) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (PinShownByDefault))
	FVector ExternalVelocity = FVector(0.f, 0.f, 0.f);

	/** Spring configuration asset (contains joints, springs, colliders) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spring", meta = (PinShownByDefault))
	float ExternalVelocityScale = 1.f;


	// FAnimNode_Base / SkeletalControl overrides
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void UpdateInternal(const FAnimationUpdateContext& Context) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateSkeletalControl_AnyThread(FComponentSpacePoseContext& Output, TArray<FBoneTransform>& OutBoneTransforms) override;
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

private:
	/* ---- Core helpers ---- */

	// Maintain length constraint
	static FVector ApplyLengthConstraint(const FVRMSimJointState& State, FVector TailWS, FVector HeadWS);

	// One-time per joint (lazy) init
	void InitializeState(FVRMSimJointState& JointState, const FTransform& ComponentTM, const FTransform& JointBoneCS);

	// Build bone references & chain index ranges from config
	void BuildMappings(const FBoneContainer& BoneContainer);

	// Allocate & fill initial per-joint state
	void EnsureStatesInitialized(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& CSPose);

	// Simulation pass over all springs
	void SimulateSpringsOnce(const FComponentSpacePoseContext& Context, const FTransform& ComponentTM, float DeltaTime);

	// Collision resolution against configured collider groups
	void ResolveCollisions(const FComponentSpacePoseContext& Context,
	                       FVector& NextTailWS,
	                       float JointRadius,
	                       const FVRMSpringConfig& SpringCfg,
	                       FCSPose<FCompactPose>& CSPose,
	                       const FTransform& ComponentTM,
	                       const TArray<int32>& GroupIndices) const;

	/* ---- Collision primitive helpers (signed distance; <0 = penetration) ---- */
	float CollideSphere(const FTransform& NodeXf, const struct FVRMSpringColliderSphere& Sph, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const;
	float CollideInsideSphere(const FTransform& NodeXf, const struct FVRMSpringColliderSphere& Sph, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const;
	float CollideCapsule(const FTransform& NodeXf, const struct FVRMSpringColliderCapsule& Cap, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const;
	float CollideInsideCapsule(const FTransform& NodeXf, const struct FVRMSpringColliderCapsule& Cap, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const;
	float CollidePlane(const FTransform& NodeXf, const struct FVRMSpringColliderPlane& P, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const;

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	// Debug draw helpers
	void DrawCollisionSphere(const FComponentSpacePoseContext& Context, const FTransform& NodeXf, const struct FVRMSpringColliderSphere& S) const;
	void DrawCollisionCapsule(const FComponentSpacePoseContext& Context, const FTransform& NodeXf, const struct FVRMSpringColliderCapsule& Cap) const;
	void DrawCollisionPlane(const FComponentSpacePoseContext& Context, const FTransform& NodeXf, const struct FVRMSpringColliderPlane& P) const;
	// Draw per-joint spring visuals: head, tail, optional velocity and animated-rest target
	void DrawSpringJoint(const FComponentSpacePoseContext& Context, const FTransform& ComponentTM, const FVRMSimJointState& JointState, const FVector& HeadCS, const FVector& TailCS, float JointRadius, const FVector& RestTargetCS, float DeltaTime) const;
#endif

private:
	/* ---- Runtime data ---- */
	TArray<FBoneReference>     JointBoneRefs;
	TArray<FVRMSimJointState>  JointStates;
	TArray<FSpringChainRange>  SpringChainRanges;
	TArray<FBoneWrite>         PendingBoneWrites;

	float CurrentDeltaTime = 0.f;
	bool  bEvalCalledThisFrame = false;
};
