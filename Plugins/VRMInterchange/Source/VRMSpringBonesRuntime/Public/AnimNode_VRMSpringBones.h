#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNode_SkeletalControlBase.h"
#include "VRMSpringBoneData.h"
#include "AnimNode_VRMSpringBones.generated.h"

// Per-joint transient simulation state (center-space tails; mirrors VRM ref impl)
USTRUCT()
struct FVRMSimJointState
{
	GENERATED_BODY()

	// current/prev tail positions in *center space* (or world if no center)
	FVector CurrentTail = FVector::ZeroVector;
	FVector PrevTail    = FVector::ZeroVector;

	// local rest axis (child dir in local space)
	FVector LocalBoneAxis = FVector(0, 0, 1);

	// world-space bone length (recomputed each update)
	float WorldBoneLength = 0.f;

	// rest pose cache
	FMatrix44f InitialLocalMatrix = FMatrix44f::Identity;
	FQuat4f    InitialLocalRot    = FQuat4f::Identity;
};

USTRUCT(BlueprintInternalUseOnly)
struct VRMINTERCHANGE_API FAnimNode_VRMSpringBones : public FAnimNode_SkeletalControlBase
{
	GENERATED_BODY()

	// Spring data parsed at import time (colliders, joints, springs, mappings)
	UPROPERTY(EditAnywhere, Category="VRM")
	TObjectPtr<UVRMSpringBoneData> SpringData = nullptr;

	// Master enable
	UPROPERTY(EditAnywhere, Category="VRM")
	bool bEnable = true;

	// Per-chain sim rate scale (1.0 = game dt)
	UPROPERTY(EditAnywhere, Category="VRM", meta=(ClampMin="0.0", ClampMax="4.0"))
	float TimeScale = 1.f;

	// Optional: freeze sim (still writes current pose; no verlet)
	UPROPERTY(EditAnywhere, Category="VRM")
	bool bPauseSimulation = false;

	// Runtime caches
	// Map VRM Joint index -> UE FBoneReference
	UPROPERTY(Transient)
	TArray<FBoneReference> JointBoneRefs;

	// For each joint we keep a sim state
	UPROPERTY(Transient)
	TArray<FVRMSimJointState> JointStates;

	// For each spring, precalc its joint index range into JointBoneRefs/JointStates
	struct FSpringRange { int32 First = INDEX_NONE; int32 Num = 0; int32 CenterJointIndex = INDEX_NONE; };
	TArray<FSpringRange> SpringRanges;

	// Scratch output rotations cached during Update_AnyThread; Evaluate just applies
	struct FBoneWrite { FCompactPoseBoneIndex BoneIndex; FQuat NewLocalRot; };
	TArray<FBoneWrite> PendingBoneWrites;

	// An increasing token so we only compute physics once per animation tick even if Evaluate is called multiple times
	uint64 LastSimTickId = 0;

	// FAnimNode_SkeletalControlBase overrides
	virtual void Initialize_AnyThread(const FAnimationInitializeContext& Context) override;
	virtual void CacheBones_AnyThread(const FAnimationCacheBonesContext& Context) override;
	virtual void Update_AnyThread(const FAnimationUpdateContext& Context) override;
	virtual bool IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones) override;
	virtual void EvaluateComponentSpaceInternal(FComponentSpacePoseContext& Context) override; // UE5.6 path
	virtual void GatherDebugData(FNodeDebugData& DebugData) override;

private:
	// ---- helper flow ----
	void BuildMappings(const FBoneContainer& BoneContainer);
	void EnsureStatesInitialized(const FBoneContainer& BoneContainer, const FTransform& ComponentTM);
	void SimulateSpringsOnce(const FBoneContainer& BoneContainer, const FTransform& ComponentTM, float DeltaTime);

	// ---- math helpers (TS-equivalent split out for clarity) ----
	static FMatrix44f GetParentWorldMatrix(const FTransform& ComponentTM, const FCSPose<FCompactPose>& CSPose, FCompactPoseBoneIndex BoneIdx);
	static FVector   CalcWorldHeadPos(const FCSPose<FCompactPose>& CSPose, FCompactPoseBoneIndex BoneIdx);
	static FVector   CalcWorldChildPos_OrPseudo(const FCSPose<FCompactPose>& CSPose, const FVRMSimJointState& S, FCompactPoseBoneIndex BoneIdx, bool bHasRealChild);
	static void      CalcWorldBoneLength(FVRMSimJointState& S, const FVector& HeadWS, const FVector& ChildWS);

	// center-space helpers
	static FMatrix44f GetCenterToWorldMatrix(const FCSPose<FCompactPose>& CSPose, TOptional<FCompactPoseBoneIndex> CenterIdx);
	static FMatrix44f GetWorldToCenterMatrix(const FMatrix44f& C2W);

	// verlet + external forces (stiffness/gravity)
	static FVector IntegrateVerlet(
		const FVRMSimJointState& S,
		const FVector& WorldSpaceBoneAxis,
		const FMatrix44f& CenterToWorld,
		float Stiffness, float Drag, const FVector& GravityDirWS, float GravityPower, float DeltaTime);

	// collision helpers (spec + extended colliders)
	void ResolveCollisions(
		FVector& NextTailWS,
		float JointRadius,
		const FVRMSpringConfig& Cfg,
		const FCSPose<FCompactPose>& CSPose,
		const FTransform& ComponentTM,
		const TArray<int32>& GroupIndices) const;

	// per-shape collide
	static float CollideSphere(const FTransform& NodeXf, const FVRMSpringColliderSphere& S, const FVector& TailWS, float JointRadius, FVector& OutPushDir);
	static float CollideCapsule(const FTransform& NodeXf, const FVRMSpringColliderCapsule& C, const FVector& TailWS, float JointRadius, FVector& OutPushDir);
	static float CollideInsideSphere(const FTransform& NodeXf, const FVRMSpringColliderSphere& S, const FVector& TailWS, float JointRadius, FVector& OutPushDir);
	static float CollideInsideCapsule(const FTransform& NodeXf, const FVRMSpringColliderCapsule& C, const FVector& TailWS, float JointRadius, FVector& OutPushDir);
	static float CollidePlane(const FTransform& NodeXf, const FVRMSpringColliderPlane& P, const FVector& TailWS, float JointRadius, FVector& OutPushDir);

	// apply rotation from tail dir (TS: fromToQuaternion of boneAxis->to)
	static FQuat ComputeLocalFromTail(
		const FVRMSimJointState& S,
		const FVector& NextTailWS,
		const FMatrix44f& ParentWorldXf,
		const FMatrix44f& InitialLocalMatrix);
};
