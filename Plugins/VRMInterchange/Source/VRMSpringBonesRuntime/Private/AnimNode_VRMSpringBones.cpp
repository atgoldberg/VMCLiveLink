#include "AnimNode_VRMSpringBones.h"
#include "Animation/AnimInstanceProxy.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"

/* ============================================================================
 *  VRM Spring Bones Runtime - Core Simulation Node Implementation
 * ============================================================================ */

#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
#define VRMSB_DRAW_SPHERE(Context, NodeXf, S) if (CVarVRMSB_DrawColliders.GetValueOnAnyThread() != 0) DrawCollisionSphere(Context, NodeXf, S)
#define VRMSB_DRAW_CAPSULE(Context, NodeXf, Cap) if (CVarVRMSB_DrawColliders.GetValueOnAnyThread() != 0) DrawCollisionCapsule(Context, NodeXf, Cap)
#define VRMSB_DRAW_PLANE(Context, NodeXf, P) if (CVarVRMSB_DrawColliders.GetValueOnAnyThread() != 0) DrawCollisionPlane(Context, NodeXf, P)

static TAutoConsoleVariable<int32> CVarVRMSB_DrawColliders(
	TEXT("vrm.SpringBones.DrawColliders"),
	0,
	TEXT("Draw debug spring bone colliders.\n")
	TEXT("0 = off, 1 = on."),
	ECVF_Default);

static TAutoConsoleVariable<int32> CVarVRMSB_DrawSprings(
	TEXT("vrm.SpringBones.DrawSprings"),
	0,
	TEXT("Draw debug spring joints.\n")
	TEXT("0 = off, 1 = head/tail, 2 = +velocity trail, 3 = +animated target"),
	ECVF_Default);

#define VRMSB_DRAW_SPRING(Context, ComponentTM, JointState, HeadCS, TailCS, JointRadius, RestTargetCS, Dt) \
    if (CVarVRMSB_DrawSprings.GetValueOnAnyThread() != 0) DrawSpringJoint(Context, ComponentTM, JointState, HeadCS, TailCS, JointRadius, RestTargetCS, Dt)
#else
#define VRMSB_DRAW_SPHERE(Context, NodeXf, S) ((void)0)
#define VRMSB_DRAW_CAPSULE(Context, NodeXf, Cap) ((void)0)
#define VRMSB_DRAW_PLANE(Context, NodeXf, P) ((void)0)
#define VRMSB_DRAW_SPRING(Context, ComponentTM, JointState, HeadCS, TailCS, JointRadius, RestTargetCS, Dt) ((void)0)
#endif

#define LOCTEXT_NAMESPACE "AnimNode_VRMSpringBones"

/* ---------------------------------------------------------------------------
 *  Static helpers
 * --------------------------------------------------------------------------- */

FVector FAnimNode_VRMSpringBones::ApplyLengthConstraint(const FVRMSimJointState& State, const FVector TailWS, const FVector HeadWS)
{
	const FVector Dir = (TailWS - HeadWS).GetSafeNormal();
	if (!Dir.IsNearlyZero())
	{
		return HeadWS + Dir * State.WorldBoneLength;
	}
	return TailWS;
}

/** One-time per joint state initialization (tail, previous frame data). */
void FAnimNode_VRMSpringBones::InitializeState(FVRMSimJointState& JointState, const FTransform& ComponentTM, const FTransform& JointBoneCS)
{
	if (!JointState.bInitialized)
	{
		const FQuat JointBoneRotCS = JointBoneCS.GetRotation();
		const FVector JointHeadPosCS = JointBoneCS.GetLocation();

		JointState.PrevHeadCS = JointHeadPosCS;

		const FVector InitialJointTailPosCS = JointHeadPosCS + JointBoneRotCS.RotateVector(FVector(JointState.InitialLocalChildPos));
		JointState.CurrentTail = InitialJointTailPosCS;
		JointState.PrevTail = InitialJointTailPosCS;

		JointState.bInitialized = 1;
	}
}

/* ---------------------------------------------------------------------------
 *  FAnimNode_VRMSpringBones overrides
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);
	JointBoneRefs.Reset();
	JointStates.Reset();
	SpringChainRanges.Reset();
	PendingBoneWrites.Reset();
	GetEvaluateGraphExposedInputs().Execute(Context);
}

void FAnimNode_VRMSpringBones::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Super::CacheBones_AnyThread(Context);
	if (!SpringData || !SpringData->SpringConfig.IsValid()) return;
	BuildMappings(Context.AnimInstanceProxy->GetRequiredBones());
}

void FAnimNode_VRMSpringBones::UpdateInternal(const FAnimationUpdateContext& Context)
{
	Super::UpdateInternal(Context);
	GetEvaluateGraphExposedInputs().Execute(Context);
	CurrentDeltaTime = Context.GetDeltaTime();
	bEvalCalledThisFrame = false;
}

bool FAnimNode_VRMSpringBones::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return bEnable && SpringData && SpringData->SpringConfig.IsValid();
}

void FAnimNode_VRMSpringBones::GatherDebugData(FNodeDebugData& DebugData)
{
	Super::GatherDebugData(DebugData);
	DebugData.AddDebugItem(FString::Printf(TEXT("VRMSpringBones: %d writes"), PendingBoneWrites.Num()));
}

/* ---------------------------------------------------------------------------
 *  Mapping / joint state preparation
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::BuildMappings(const FBoneContainer& BoneContainer)
{
	JointBoneRefs.Reset();
	SpringChainRanges.Reset();

	const FVRMSpringConfig& SpringCfg = SpringData->SpringConfig;

	JointBoneRefs.SetNum(SpringCfg.Joints.Num());
	for (int32 J = 0; J < SpringCfg.Joints.Num(); ++J)
	{
		const FName BoneName = (SpringCfg.Joints[J].BoneName.IsNone() && SpringCfg.Joints[J].NodeIndex != INDEX_NONE)
			? SpringData->GetBoneNameForNode(SpringCfg.Joints[J].NodeIndex)
			: SpringCfg.Joints[J].BoneName;

		FBoneReference Ref; Ref.BoneName = BoneName; Ref.Initialize(BoneContainer);
		JointBoneRefs[J] = Ref;
	}

	SpringChainRanges.SetNum(SpringCfg.Springs.Num());
	int32 Cursor = 0;
	for (int32 SIdx = 0; SIdx < SpringCfg.Springs.Num(); ++SIdx)
	{
		const FVRMSpring& Spring = SpringCfg.Springs[SIdx];
		FSpringChainRange SpringChainRng;
		SpringChainRng.First = Cursor;
		SpringChainRng.Num = Spring.JointIndices.Num();
		SpringChainRanges[SIdx] = SpringChainRng;
		Cursor += SpringChainRng.Num;
	}
}

void FAnimNode_VRMSpringBones::EnsureStatesInitialized(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& CSPose)
{
	if (JointStates.Num() == JointBoneRefs.Num()) return;

	const FVRMSpringConfig& SpringCfg = SpringData->SpringConfig;

	TMap<int32,int32> JointToSpring;
	for (int32 SpringIdx = 0; SpringIdx < SpringChainRanges.Num(); ++SpringIdx)
	{
		const FSpringChainRange& SpringChainRng = SpringChainRanges[SpringIdx];
		for (int32 i=0;i<SpringChainRng.Num;++i)
		{
			const int32 JointIdx = SpringCfg.Springs[SpringIdx].JointIndices[i];
			JointToSpring.Add(JointIdx,SpringIdx);
		}
	}

	JointStates.SetNum(JointBoneRefs.Num());

	for (int32 JointIdx = 0; JointIdx < JointBoneRefs.Num(); ++JointIdx)
	{
		const FBoneReference& BoneRef = JointBoneRefs[JointIdx];
		if (!BoneRef.HasValidSetup()) continue;

		FVRMSimJointState& JointState = JointStates[JointIdx];
		const FCompactPoseBoneIndex BoneIdx = BoneRef.GetCompactPoseIndex(BoneContainer);

		const FTransform BoneCS = CSPose.GetComponentSpaceTransform(BoneIdx);

		// Parent
		FTransform ParentCS = FTransform::Identity;
		const FCompactPoseBoneIndex ParentIdx = BoneContainer.GetParentBoneIndex(BoneIdx);
		if (ParentIdx.IsValid())
		{
			ParentCS = CSPose.GetComponentSpaceTransform(ParentIdx);
		}

		const FTransform LocalRest = BoneCS.GetRelativeTransform(ParentCS);

		const FVector HeadCS = BoneCS.GetLocation();

		FVector ChildCS;
		bool bHasRealChild = false;
		if (const int32* SpringIdxPtr = JointToSpring.Find(JointIdx))
		{
			const FVRMSpring& Spring = SpringCfg.Springs[*SpringIdxPtr];
			const FSpringChainRange& SpringChainRng = SpringChainRanges[*SpringIdxPtr];
			for (int32 i=0;i<SpringChainRng.Num-1;++i)
			{
				if (Spring.JointIndices[i] == JointIdx)
				{
					const int32 ChildJoint = Spring.JointIndices[i+1];
					if (JointBoneRefs.IsValidIndex(ChildJoint))
					{
						const FBoneReference& ChildBoneRef = JointBoneRefs[ChildJoint];
						if (ChildBoneRef.HasValidSetup())
						{
							const FCompactPoseBoneIndex ChildIdx = ChildBoneRef.GetCompactPoseIndex(BoneContainer);
							ChildCS = CSPose.GetComponentSpaceTransform(ChildIdx).GetLocation();
							bHasRealChild = true;
						}
					}
					break;
				}
			}
		}

		if (bHasRealChild)
		{
			const FVector AxisCS = (ChildCS - HeadCS).GetSafeNormal();
			const FQuat LocalInv = LocalRest.GetRotation().Inverse();
			JointState.BoneAxisLocal = LocalInv.RotateVector(AxisCS).GetSafeNormal();
			JointState.WorldBoneLength = (ChildCS - HeadCS).Length();
			JointState.InitialLocalChildPos = LocalRest.InverseTransformPosition(ChildCS - ParentCS.GetLocation());
		}
		else
		{
			// Derive a reasonable axis from the parent->this direction if possible; fallback to this joint's rest forward.
			const float VirtualTailLengthCm = 0.07f * 100.f; // 7 cm in UE units (centimeters)

			FVector AxisCS = FVector::ZeroVector;
			// If we have a valid parent bone transform, use the incoming chain segment direction
			if (ParentIdx.IsValid())
			{
				const FVector ParentHeadCS = ParentCS.GetLocation();
				AxisCS = (HeadCS - ParentHeadCS).GetSafeNormal();
			}
			// Fallback: use this bone's rest rotation forward (+X in local space rotated into component space)
			if (AxisCS.IsNearlyZero())
			{
				AxisCS = BoneCS.GetRotation().RotateVector(FVector(1,0,0)).GetSafeNormal();
			}
			if (AxisCS.IsNearlyZero())
			{
				AxisCS = FVector(1,0,0); // Final safety fallback
			}

			// Convert component-space axis into the joint's local space (same pattern as real-child branch)
			const FQuat LocalInv = LocalRest.GetRotation().Inverse();
			JointState.BoneAxisLocal = LocalInv.RotateVector(AxisCS).GetSafeNormal();
			if (!JointState.BoneAxisLocal.IsNormalized())
			{
				JointState.BoneAxisLocal = FVector(1,0,0);
			}

			JointState.WorldBoneLength = VirtualTailLengthCm;

			// Build a virtual child position in component space to derive InitialLocalChildPos consistently.
			const FVector VirtualChildCS = HeadCS + AxisCS * VirtualTailLengthCm;
			// Child (or virtual child) expressed relative to parent CS then into this bone's local space:
			JointState.InitialLocalChildPos = LocalRest.InverseTransformPosition(VirtualChildCS - ParentCS.GetLocation());
		}

		JointState.CurrentTail = HeadCS + BoneCS.GetRotation().RotateVector(FVector(JointState.InitialLocalChildPos));
		JointState.PrevTail    = JointState.CurrentTail;
	}
}

/* ---------------------------------------------------------------------------
 *  Simulation
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::SimulateSpringsOnce(const FComponentSpacePoseContext& Context,
                                                   const FTransform& ComponentTM,
                                                   const float DeltaTime)
{
	FCSPose<FCompactPose> CSPose = Context.Pose;
	const FVRMSpringConfig& SpringCfg = SpringData->SpringConfig;
	PendingBoneWrites.Reset();

	const FBoneContainer& BoneContainer = CSPose.GetPose().GetBoneContainer();

	for (int32 SpringIdx = 0; SpringIdx < SpringChainRanges.Num(); ++SpringIdx)
	{
		const FSpringChainRange& SpringChainRng = SpringChainRanges[SpringIdx];
		if (SpringChainRng.Num <= 0) continue;
		const FVRMSpring& Spring = SpringCfg.Springs[SpringIdx];

		const float Stiffness      = Spring.Stiffness;
		const float Drag           = FMath::Clamp(Spring.Drag, 0.f, 1.f);
		const float GravityPower   = Spring.GravityPower * 100.0f;
		const bool  bHasColliders  = Spring.ColliderGroupIndices.Num() > 0;
		const FVector GravityDir   = Spring.GravityDir;
		const float DefaultHitRadius = FMath::Max(0.f, Spring.HitRadius * 100.0f);

		for (int32 ChainPos = 0; ChainPos < SpringChainRng.Num; ++ChainPos)
		{
			const bool bIsSpringRoot = (ChainPos == 0);
			const int32 JointIndex = Spring.JointIndices[ChainPos];
			if (!JointBoneRefs.IsValidIndex(JointIndex)) continue;

			const FBoneReference& JointBoneRef = JointBoneRefs[JointIndex];
			if (!JointBoneRef.HasValidSetup()) continue;

			FVRMSimJointState& JointState = JointStates[JointIndex];
			FVRMSimJointState& ParentState = bIsSpringRoot ? JointState : JointStates[Spring.JointIndices[ChainPos-1]];
			const FCompactPoseBoneIndex JointBoneIdx = JointBoneRef.GetCompactPoseIndex(BoneContainer);

			FTransform ParentBoneCS = FTransform::Identity;
			const FCompactPoseBoneIndex ParentBoneIdx = BoneContainer.GetParentBoneIndex(JointBoneIdx);
			if (ParentBoneIdx.IsValid())
			{
				ParentBoneCS = CSPose.GetComponentSpaceTransform(ParentBoneIdx);
			}

			const FTransform JointBoneCS = CSPose.GetComponentSpaceTransform(JointBoneIdx);

			InitializeState(JointState, ComponentTM, JointBoneCS);

			const FVector CurrentHeadPosCS = bIsSpringRoot ? JointBoneCS.GetTranslation() : ParentState.PrevTail;

			FVector Inertia = (JointState.CurrentTail - JointState.PrevTail) * (1.f - Drag);
			FVector Gravity = GravityDir * GravityPower * DeltaTime;

			const FQuat AnimatedBoneRotCS = JointBoneCS.GetRotation();
			const FVector RestTargetCS = CurrentHeadPosCS + AnimatedBoneRotCS.RotateVector(JointState.InitialLocalChildPos);

			FVector SimTailPos = JointState.CurrentTail + Inertia + Gravity;
			if (!RestTargetCS.ContainsNaN())
			{
				const float StiffScale = Stiffness * DeltaTime;
				SimTailPos += (RestTargetCS - SimTailPos) * StiffScale;
			}

			JointState.PrevHeadCS = CurrentHeadPosCS;

			const FVector PostSimHeadPos = JointState.PrevHeadCS;
			const FQuat   BoneRotCS = JointBoneCS.GetRotation();
			FVector PostSimTailPositionFixed = ApplyLengthConstraint(JointState, SimTailPos, PostSimHeadPos);

			if (bHasColliders)
			{
				const float JointHitRadius = (JointState.WorldBoneLength <= KINDA_SMALL_NUMBER)
					? DefaultHitRadius
					: FMath::Min(DefaultHitRadius, JointState.WorldBoneLength * 0.5f);
				FVector NextTailWS = ComponentTM.TransformPosition(PostSimTailPositionFixed);
				ResolveCollisions(Context, NextTailWS, JointHitRadius, SpringCfg, CSPose, ComponentTM, Spring.ColliderGroupIndices);
				PostSimTailPositionFixed = ComponentTM.InverseTransformPosition(NextTailWS);
			}

			const FVector PostCollideHeadPos = JointState.PrevHeadCS;
			FVector PostCollideTailPositionFixed = ApplyLengthConstraint(JointState, PostSimTailPositionFixed, PostCollideHeadPos);

			JointState.PrevTail = JointState.CurrentTail;
			JointState.CurrentTail = PostCollideTailPositionFixed;

			const FVector AxisCS = BoneRotCS.RotateVector(JointState.BoneAxisLocal);
			const FVector TargetDir = (PostCollideTailPositionFixed - PostCollideHeadPos).GetSafeNormal();
			const bool bLenNearlyZero = (PostCollideTailPositionFixed - PostCollideHeadPos).IsNearlyZero();
			const FQuat PostCollideBoneRotCS = bLenNearlyZero ? BoneRotCS : FQuat::FindBetweenVectors(AxisCS, TargetDir) * BoneRotCS;

			// Compute joint radius for drawing (matches collision calculation)
			const float JointHitRadiusForDraw = (JointState.WorldBoneLength <= KINDA_SMALL_NUMBER)
				? DefaultHitRadius
				: FMath::Min(DefaultHitRadius, JointState.WorldBoneLength * 0.5f);

			// Debug draw per-joint
			VRMSB_DRAW_SPRING(Context, ComponentTM, JointState, PostCollideHeadPos, PostCollideTailPositionFixed, JointHitRadiusForDraw, RestTargetCS, DeltaTime);

			PendingBoneWrites.Add({ JointBoneIdx, PostCollideHeadPos, PostCollideBoneRotCS });
		}
	}
}

/* ---------------------------------------------------------------------------
 *  Evaluation
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::EvaluateSkeletalControl_AnyThread(
	FComponentSpacePoseContext& Context,
	TArray<FBoneTransform>& OutBoneTransforms)
{
	if (bEvalCalledThisFrame) return;
	if (!bEnable || !SpringData || !SpringData->SpringConfig.IsValid()) return;
	if (FMath::IsNearlyZero(CurrentDeltaTime)) return;

	const FBoneContainer& BoneContainer = Context.Pose.GetPose().GetBoneContainer();
	const FTransform ComponentTM = Context.AnimInstanceProxy->GetComponentTransform();

	EnsureStatesInitialized(BoneContainer, Context.Pose);
	PendingBoneWrites.Reset();

	const float Dt = bPauseSimulation ? 0.f : CurrentDeltaTime;
	SimulateSpringsOnce(Context, ComponentTM, Dt);

	OutBoneTransforms.Reset();
	OutBoneTransforms.Reserve(PendingBoneWrites.Num());
	if (PendingBoneWrites.Num() == 0) return;

	PendingBoneWrites.Sort([](const FBoneWrite& A, const FBoneWrite& B)
	{
		return A.BoneIndex.GetInt() < B.BoneIndex.GetInt();
	});

	for (const FBoneWrite& BW : PendingBoneWrites)
	{
		FTransform NewCS = FTransform(BW.NewRotation, BW.NewPosition, FVector(1, 1, 1));
		OutBoneTransforms.Add(FBoneTransform(BW.BoneIndex, NewCS));
	}

	if (OutBoneTransforms.Num() > 1)
	{
		OutBoneTransforms.Sort(FCompareBoneTransformIndex());
	}
	bEvalCalledThisFrame = true;
}

/* ---------------------------------------------------------------------------
 *  Debug drawing
 * --------------------------------------------------------------------------- */
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
void FAnimNode_VRMSpringBones::DrawCollisionSphere(const FComponentSpacePoseContext& Context, const FTransform& NodeXf, const FVRMSpringColliderSphere& S) const
{
	if (!Context.AnimInstanceProxy) return;

	const FVector Center = NodeXf.TransformPosition(S.Offset * 100.f);
	const float Radius = S.Radius * 100.f;

	if (Radius <= 0.f)
	{
		Context.AnimInstanceProxy->AnimDrawDebugSphere(Center, 1.f, 8, FColor::Yellow, false, -1.f, 0.25f, SDPG_World);
		return;
	}
	Context.AnimInstanceProxy->AnimDrawDebugSphere(Center, Radius, 12, FColor::Green, false, -1.f, 0.25f, SDPG_World);
}

void FAnimNode_VRMSpringBones::DrawCollisionCapsule(const FComponentSpacePoseContext& Context, const FTransform& NodeXf, const FVRMSpringColliderCapsule& Cap) const
{
	if (!Context.AnimInstanceProxy) return;

	const FVector P0 = NodeXf.TransformPosition(Cap.Offset * 100.f);
	const FVector P1 = NodeXf.TransformPosition(Cap.TailOffset * 100.f);
	const float Radius = Cap.Radius * 100.f;

	const float SegmentLen = (P1 - P0).Size();
	const float CylinderLen = FMath::Max(0.f, SegmentLen - 2.f * Radius);
	const float HalfHeight = CylinderLen * 0.5f;
	const FVector Center = (P0 + P1) * 0.5f;

	FVector Dir = (P1 - P0).GetSafeNormal();
	if (Dir.IsNearlyZero())
	{
		Context.AnimInstanceProxy->AnimDrawDebugSphere(Center, Radius, 12, FColor::Green, false, -1.f, 0.25f, SDPG_World);
		return;
	}

	const FRotator Rotation = FRotator(FRotationMatrix::MakeFromZ(Dir).ToQuat());
	Context.AnimInstanceProxy->AnimDrawDebugCapsule(Center, HalfHeight, Radius, Rotation, FColor::Green, false, -1.f, 0.25f, SDPG_World);
}

void FAnimNode_VRMSpringBones::DrawCollisionPlane(const FComponentSpacePoseContext& Context, const FTransform& NodeXf, const FVRMSpringColliderPlane& P) const
{
	if (!Context.AnimInstanceProxy) return;

	const FVector Center = NodeXf.TransformPosition(P.Offset * 100.f);
	FVector NormalWS = NodeXf.TransformVectorNoScale(P.Normal).GetSafeNormal();
	if (NormalWS.IsNearlyZero()) NormalWS = FVector(0, 0, 1);

	const float HalfSize = 25.f * 100.f;

	FVector Tangent = FVector::CrossProduct(NormalWS, FVector(0, 1, 0));
	if (Tangent.IsNearlyZero()) Tangent = FVector::CrossProduct(NormalWS, FVector(1, 0, 0));
	Tangent.Normalize();
	const FVector Bitangent = FVector::CrossProduct(NormalWS, Tangent).GetSafeNormal();

	const FVector C0 = Center + (Tangent * HalfSize) + (Bitangent * HalfSize);
	const FVector C1 = Center + (Tangent * HalfSize) - (Bitangent * HalfSize);
	const FVector C2 = Center - (Tangent * HalfSize) - (Bitangent * HalfSize);
	const FVector C3 = Center - (Tangent * HalfSize) + (Bitangent * HalfSize);

	const FColor PlaneColor = FColor::Blue;
	const float LifeTime = 0.f;
	const uint8 DepthPriority = 0;
	const float Thickness = 2.f;
	Context.AnimInstanceProxy->AnimDrawDebugLine(C0, C1, PlaneColor, false, LifeTime, Thickness, SDPG_World);
	Context.AnimInstanceProxy->AnimDrawDebugLine(C1, C2, PlaneColor, false, LifeTime, Thickness, SDPG_World);
	Context.AnimInstanceProxy->AnimDrawDebugLine(C2, C3, PlaneColor, false, LifeTime, Thickness, SDPG_World);
	Context.AnimInstanceProxy->AnimDrawDebugLine(C3, C0, PlaneColor, false, LifeTime, Thickness, SDPG_World);

	const float ArrowSize = FMath::Max(50.f, HalfSize * 0.25f);
	Context.AnimInstanceProxy->AnimDrawDebugDirectionalArrow(Center, Center + NormalWS * ArrowSize, ArrowSize * 0.25f, PlaneColor, false, LifeTime, 2.f, SDPG_World);
}

// Draw debug visuals for a single spring joint: head (red), tail (yellow sized by joint radius), optional velocity line and animated-rest target (cyan)
void FAnimNode_VRMSpringBones::DrawSpringJoint(const FComponentSpacePoseContext& Context, const FTransform& ComponentTM, const FVRMSimJointState& JointState, const FVector& HeadCS, const FVector& TailCS, float JointRadius, const FVector& RestTargetCS, float DeltaTime) const
{
	if (!Context.AnimInstanceProxy) return;

	const int32 Mode = CVarVRMSB_DrawSprings.GetValueOnAnyThread();
	if (Mode == 0) return;

	const FVector HeadWS = ComponentTM.TransformPosition(HeadCS);
	const FVector TailWS = ComponentTM.TransformPosition(TailCS);
	Context.AnimInstanceProxy->AnimDrawDebugSphere(HeadWS, FMath::Max(1.f, JointRadius * 0.2f), 8, FColor::Red, false, -1.f, 0.25f, SDPG_World);
	// Head: red small sphere

	// Tail: yellow sphere sized to joint radius
	Context.AnimInstanceProxy->AnimDrawDebugSphere(TailWS, FMath::Max(1.f, JointRadius), 12, FColor::Yellow, false, -1.f, 0.25f, SDPG_World);

	// Red line from head to tail
	Context.AnimInstanceProxy->AnimDrawDebugLine(HeadWS, TailWS, FColor::Red, false, -1.f, 0.5f, SDPG_World);

	// Velocity trail when mode >= 2
	if (Mode >= 2 && DeltaTime > KINDA_SMALL_NUMBER)
	{
		// Approximate velocity in world space using CS delta transformed by component TM
		FVector PrevTailCS = JointState.PrevTail;
		FVector PrevTailWS = ComponentTM.TransformPosition(PrevTailCS);
		FVector VelocityWS = (TailWS - PrevTailWS) / DeltaTime; // world units per second
		const float VelScale = 0.05f; // scale so line isn't excessively long
		const FVector End = TailWS + VelocityWS * VelScale;
		// Keep this line around for a short while to create a trail
		const float LifeTime = 1.f; // seconds
		Context.AnimInstanceProxy->AnimDrawDebugLine(TailWS, End, FColor::Magenta, false, LifeTime, 0.f, SDPG_World);
	}

	// Animated target when mode == 3
	if (Mode == 3)
	{
		const FVector TargetWS = ComponentTM.TransformPosition(RestTargetCS);
		Context.AnimInstanceProxy->AnimDrawDebugSphere(TargetWS, FMath::Max(1.f, JointRadius * 0.25f), 8, FColor::Cyan, false, -1.f, 0.15f, SDPG_World);
	}
}
#endif

/* ---------------------------------------------------------------------------
 *  Collision resolution
 * --------------------------------------------------------------------------- */

void FAnimNode_VRMSpringBones::ResolveCollisions(
	const FComponentSpacePoseContext& Context,
	FVector& NextTailWS,
	float JointRadius,
	const FVRMSpringConfig& SpringCfg,
	FCSPose<FCompactPose>& CSPose,
	const FTransform& ComponentTM,
	const TArray<int32>& GroupIndices) const
{
	for (int32 GIdx : GroupIndices)
	{
		if (!SpringCfg.ColliderGroups.IsValidIndex(GIdx)) continue;
		const FVRMSpringColliderGroup& Group = SpringCfg.ColliderGroups[GIdx];

		for (int32 CIdx : Group.ColliderIndices)
		{
			if (!SpringCfg.Colliders.IsValidIndex(CIdx)) continue;
			const FVRMSpringCollider& Col = SpringCfg.Colliders[CIdx];

			FTransform NodeXf = ComponentTM;
			if (!Col.BoneName.IsNone())
			{
				FBoneReference BR; BR.BoneName = Col.BoneName; BR.Initialize(CSPose.GetPose().GetBoneContainer());
				if (BR.HasValidSetup())
				{
					NodeXf = CSPose.GetComponentSpaceTransform(BR.GetCompactPoseIndex(CSPose.GetPose().GetBoneContainer())) * ComponentTM;
				}
			}

			FVector PushDir; float Pen;

			for (const auto& S : Col.Spheres)
			{
				Pen = S.bInside
					? CollideInsideSphere(NodeXf, S, NextTailWS, JointRadius, PushDir)
					: CollideSphere(NodeXf, S, NextTailWS, JointRadius, PushDir);
				if (Pen < 0.f) NextTailWS -= PushDir * Pen;
				VRMSB_DRAW_SPHERE(Context, NodeXf, S);
			}
			for (const auto& Cap : Col.Capsules)
			{
				Pen = Cap.bInside
					? CollideInsideCapsule(NodeXf, Cap, NextTailWS, JointRadius, PushDir)
					: CollideCapsule(NodeXf, Cap, NextTailWS, JointRadius, PushDir);
				if (Pen < 0.f) NextTailWS -= PushDir * Pen;
				VRMSB_DRAW_CAPSULE(Context, NodeXf, Cap);
			}
			for (const auto& Pl : Col.Planes)
			{
				Pen = CollidePlane(NodeXf, Pl, NextTailWS, JointRadius, PushDir);
				if (Pen < 0.f) NextTailWS -= PushDir * Pen;
				VRMSB_DRAW_PLANE(Context, NodeXf, Pl);
			}
		}
	}
}

/* ---------------------------------------------------------------------------
 *  Collision primitive helpers
 * --------------------------------------------------------------------------- */

float FAnimNode_VRMSpringBones::CollideSphere(const FTransform& NodeXf, const FVRMSpringColliderSphere& Sph, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector CenterWS = NodeXf.TransformPosition(Sph.Offset * 100.f);
	const FVector Delta = TailWS - CenterWS;
	const float Distance = Delta.Length() - (Sph.Radius * 100.f + JointRadius);
	OutPushDir = Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollideInsideSphere(const FTransform& NodeXf, const FVRMSpringColliderSphere& Sph, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector CenterWS = NodeXf.TransformPosition(Sph.Offset * 100.f);
	const FVector Delta = TailWS - CenterWS;
	const float Distance = (Sph.Radius * 100.f - JointRadius) - Delta.Length();
	OutPushDir = -Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollideCapsule(const FTransform& NodeXf, const FVRMSpringColliderCapsule& Cap, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector HeadWS = NodeXf.TransformPosition(Cap.Offset * 100.f);
	const FVector TailC  = NodeXf.TransformPosition(Cap.TailOffset * 100.f);
	const FVector AtoB   = TailC - HeadWS;
	FVector Delta = TailWS - HeadWS;
	const float Dot = FVector::DotProduct(AtoB, Delta);
	if (Dot > 0.f)
	{
		const float SegLenSq = AtoB.SizeSquared();
		if (Dot > SegLenSq) { Delta -= AtoB; }
		else { Delta -= AtoB * (Dot / SegLenSq); }
	}
	const float Distance = Delta.Length() - (Cap.Radius * 100.f + JointRadius);
	OutPushDir = Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollideInsideCapsule(const FTransform& NodeXf, const FVRMSpringColliderCapsule& Cap, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector HeadWS = NodeXf.TransformPosition(Cap.Offset * 100.f);
	const FVector TailC  = NodeXf.TransformPosition(Cap.TailOffset * 100.f);
	const FVector AtoB   = TailC - HeadWS;
	FVector Delta = TailWS - HeadWS;
	const float Dot = FVector::DotProduct(AtoB, Delta);
	if (Dot > 0.f)
	{
		const float SegLenSq = AtoB.SizeSquared();
		if (Dot > SegLenSq) { Delta -= AtoB; }
		else { Delta -= AtoB * (Dot / SegLenSq); }
	}
	const float Distance = (Cap.Radius * 100.f - JointRadius) - Delta.Length();
	OutPushDir = -Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollidePlane(const FTransform& NodeXf, const FVRMSpringColliderPlane& P, const FVector& TailWS, float JointRadius, FVector& OutPushDir) const
{
	const FVector OffsetWS = NodeXf.TransformPosition(P.Offset * 100.f);
	FVector NormalWS = NodeXf.TransformVectorNoScale(P.Normal).GetSafeNormal();
	if (NormalWS.IsNearlyZero()) NormalWS = FVector(0,0,1);
	const FVector Delta = TailWS - OffsetWS;
	const float Distance = FVector::DotProduct(Delta, NormalWS) - JointRadius;
	OutPushDir = NormalWS;
	return Distance;
}

#undef LOCTEXT_NAMESPACE
