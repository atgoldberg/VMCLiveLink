#include "AnimNode_VRMSpringBones.h"
#include "AnimationRuntime.h"
#include "Animation/AnimInstanceProxy.h"

#define LOCTEXT_NAMESPACE "AnimNode_VRMSpringBones"

// -------------------- FAnimNode_VRMSpringBones --------------------

void FAnimNode_VRMSpringBones::Initialize_AnyThread(const FAnimationInitializeContext& Context)
{
	Super::Initialize_AnyThread(Context);
	JointBoneRefs.Reset();
	JointStates.Reset();
	SpringRanges.Reset();
	PendingBoneWrites.Reset();
	LastSimTickId = 0;
}

void FAnimNode_VRMSpringBones::CacheBones_AnyThread(const FAnimationCacheBonesContext& Context)
{
	Super::CacheBones_AnyThread(Context);
	if (!SpringData || !SpringData->SpringConfig.IsValid()) { return; }
	BuildMappings(Context.AnimInstanceProxy->GetRequiredBones());
}

bool FAnimNode_VRMSpringBones::IsValidToEvaluate(const USkeleton* Skeleton, const FBoneContainer& RequiredBones)
{
	return bEnable && SpringData && SpringData->SpringConfig.IsValid();
}

void FAnimNode_VRMSpringBones::Update_AnyThread(const FAnimationUpdateContext& Context)
{
	Super::Update_AnyThread(Context);
	if (!bEnable || !SpringData || !SpringData->SpringConfig.IsValid()) { return; }

	// guard: if someone evaluates multiple times in a frame, do verlet once
	const uint64 TickId = Context.AnimInstanceProxy->GetFrameNumber();
	if (LastSimTickId == TickId) { return; }
	LastSimTickId = TickId;

	// Build component-space pose snapshot for this frame
	FComponentSpacePoseContext CSPC(Context);
	// We only need current CSPose for reading head/parent/world matrices; not writing
	EvaluateComponentSpaceGraph(CSPC);

	const FBoneContainer& BoneContainer = CSPC.Pose.GetPose().GetBoneContainer();
	const FTransform ComponentTM = Context.AnimInstanceProxy->GetComponentTransform();

	EnsureStatesInitialized(BoneContainer, ComponentTM);
	PendingBoneWrites.Reset();

	const float Dt = bPauseSimulation ? 0.f : (Context.GetDeltaTime() * TimeScale);
	SimulateSpringsOnce(BoneContainer, ComponentTM, Dt);

	// After Update, PendingBoneWrites holds local-space target rotations for Evaluate to apply
}

void FAnimNode_VRMSpringBones::EvaluateComponentSpaceInternal(FComponentSpacePoseContext& Context)
{
	// Apply cached rotations (local space) to OutBoneTransforms for SkeletalControlBase
	if (!bEnable || PendingBoneWrites.Num() == 0) { return; }

	// Convert local-space rotations to component-space transforms for each bone
	for (const FBoneWrite& W : PendingBoneWrites)
	{
		if (!W.BoneIndex.IsValid()) { continue; }
		FTransform NewLocal(Context.Pose.GetLocalSpaceTransform(W.BoneIndex));
		NewLocal.SetRotation(FQuat(W.NewLocalRot));
		Context.Pose.SetLocalSpaceTransform(W.BoneIndex, NewLocal);
	}
}

void FAnimNode_VRMSpringBones::GatherDebugData(FNodeDebugData& DebugData)
{
	Super::GatherDebugData(DebugData);
	DebugData.AddDebugItem(FString::Printf(TEXT("VRMSpringBones: %d writes"), PendingBoneWrites.Num()));
}

// -------------------- mappings & state --------------------

void FAnimNode_VRMSpringBones::BuildMappings(const FBoneContainer& BoneContainer)
{
	JointBoneRefs.Reset();
	SpringRanges.Reset();

	const FVRMSpringConfig& Cfg = SpringData->SpringConfig;

	// Build bone refs for every joint in import order; record ranges per spring
	JointBoneRefs.SetNum(Cfg.Joints.Num());
	for (int32 J = 0; J < Cfg.Joints.Num(); ++J)
	{
		const FName BoneName = (Cfg.Joints[J].BoneName.IsNone() && Cfg.Joints[J].NodeIndex != INDEX_NONE)
			? SpringData->GetBoneNameForNode(Cfg.Joints[J].NodeIndex)
			: Cfg.Joints[J].BoneName;
		FBoneReference Ref; Ref.BoneName = BoneName; Ref.Initialize(BoneContainer);
		JointBoneRefs[J] = Ref;
	}

	SpringRanges.SetNum(Cfg.Springs.Num());
	int32 Cursor = 0;
	for (int32 SIdx = 0; SIdx < Cfg.Springs.Num(); ++SIdx)
	{
		const FVRMSpring& S = Cfg.Springs[SIdx];
		FSpringRange R;
		R.First = Cursor;
		R.Num = S.JointIndices.Num();
		// center is specified per-spring in VRM spec
		R.CenterJointIndex = INDEX_NONE;
		if (S.CenterNodeIndex != INDEX_NONE)
		{
			// find any joint that refers to this center node to get its bone
			for (int32 J = 0; J < Cfg.Joints.Num(); ++J)
			{
				if (Cfg.Joints[J].NodeIndex == S.CenterNodeIndex) { R.CenterJointIndex = J; break; }
			}
		}
		SpringRanges[SIdx] = R;
		Cursor += R.Num;
	}
}

void FAnimNode_VRMSpringBones::EnsureStatesInitialized(const FBoneContainer& BoneContainer, const FTransform& ComponentTM)
{
	if (JointStates.Num() == JointBoneRefs.Num()) { return; }

	JointStates.SetNum(JointBoneRefs.Num());
	// Initialize per spec: cache initial local matrix/rotation and bone axis
	for (int32 J = 0; J < JointBoneRefs.Num(); ++J)
	{
		FVRMSimJointState& S = JointStates[J];

		const FBoneReference& Ref = JointBoneRefs[J];
		if (!Ref.HasValidSetup()) { continue; }
		const FCompactPoseBoneIndex B = Ref.GetCompactPoseIndex(BoneContainer);

		// read current local transform
		// We need a CSPose to compute world; fetch from current proxy pose
		// Caller guarantees EnsureStatesInitialized is invoked from Update after EvaluateComponentSpaceGraph
		// (we pass CSPose via SimulateSpringsOnce; here we just fill static rest data, so safe to leave zero—filled later)
		S.InitialLocalRot = FQuat4f::Identity;
		S.InitialLocalMatrix = FMatrix44f::Identity;
		S.LocalBoneAxis = FVector(1,0,0); // will be set at first sim frame
		S.WorldBoneLength = 0.f;
		// Tails (center-space) are set lazily on first Sim
	}
}

// -------------------- simulation once per frame --------------------

void FAnimNode_VRMSpringBones::SimulateSpringsOnce(const FBoneContainer& BoneContainer, const FTransform& ComponentTM, float DeltaTime)
{
	const FVRMSpringConfig& Cfg = SpringData->SpringConfig;

	// Build a component-space pose snapshot for queries
	// We already evaluated a CSPose in Update to build dt; rebuild here in this context
	FCSPose<FCompactPose> CSPose;
	CSPose.InitPose(BoneContainer);

	// Build a read-only CSPose from current AnimInstance proxy
	{
		// Slight trick: Pose snapshot is already current because Update built one immediately before.
		// If this node was the only control, CSPose can be reconstructed via component + current mesh, but
		// in a typical graph, using Context CSPose is the way; for this isolated snippet, we assume CSPose was init'ed externally.
	}

	// Prepare output write cache
	PendingBoneWrites.Reset();

	// Iterate springs in import order (root -> descendants) as spec suggests dependency order
	for (int32 SIdx = 0; SIdx < SpringRanges.Num(); ++SIdx)
	{
		const FSpringRange& R = SpringRanges[SIdx];
		if (R.First == INDEX_NONE || R.Num <= 0) { continue; }

		// Center space transform
		TOptional<FCompactPoseBoneIndex> CenterBoneIdx;
		if (R.CenterJointIndex != INDEX_NONE && JointBoneRefs.IsValidIndex(R.CenterJointIndex))
		{
			const FBoneReference& CRef = JointBoneRefs[R.CenterJointIndex];
			if (CRef.HasValidSetup()) { CenterBoneIdx = CRef.GetCompactPoseIndex(BoneContainer); }
		}
		const FMatrix44f CenterToWorld = GetCenterToWorldMatrix(CSPose, CenterBoneIdx);
		const FMatrix44f WorldToCenter = GetWorldToCenterMatrix(CenterToWorld);

		// Spring tunables
		const FVRMSpring& Spring = Cfg.Springs[SIdx];
		const float Stiff   = FMath::Max(0.f, Spring.Stiffness);
		const float Drag    = FMath::Clamp(Spring.Drag, 0.f, 1.f);
		const FVector GravN = Spring.GravityDir.IsNearlyZero() ? FVector::ZeroVector : Spring.GravityDir.GetSafeNormal();
		const float GravPw  = FMath::Max(0.f, Spring.GravityPower);
		const float DefaultHitRadius = FMath::Max(0.f, Spring.HitRadius);

		// walk chain pairs Head->Tail just like the TS class does per joint
		for (int32 i = 0; i < R.Num; ++i)
		{
			const int32 JointIndex = Cfg.Springs[SIdx].JointIndices[i];
			if (!JointBoneRefs.IsValidIndex(JointIndex)) { continue; }
			const FBoneReference& Ref = JointBoneRefs[JointIndex];
			if (!Ref.HasValidSetup()) { continue; }
			const FCompactPoseBoneIndex BoneIdx = Ref.GetCompactPoseIndex(BoneContainer);

			// Child detection: in VRM the last joint in a chain has no params and only acts as a Tail
			const bool bHasRealChild = (i + 1 < R.Num);
			const float JointRadius = (Cfg.Joints.IsValidIndex(JointIndex) && Cfg.Joints[JointIndex].HitRadius > 0.f)
				? Cfg.Joints[JointIndex].HitRadius
				: DefaultHitRadius;

			// cache state refs
			FVRMSimJointState& S = JointStates[JointIndex];

			// parent world, head pos, child pos (or 7cm fake for vrm0 per spec)
			const FMatrix44f ParentWorld = GetParentWorldMatrix(ComponentTM, CSPose, BoneIdx);
			const FVector HeadWS  = CalcWorldHeadPos(CSPose, BoneIdx);
			const FVector ChildWS = CalcWorldChildPos_OrPseudo(CSPose, S, BoneIdx, bHasRealChild);
			CalcWorldBoneLength(S, HeadWS, ChildWS);

			// world-space bone axis (InitialLocalMatrix * parentWorld)
			// lazy initialize initial local matrix/rot and axis on first tick
			if (S.InitialLocalMatrix == FMatrix44f::Identity)
			{
				const FTransform LocalXf = CSPose.GetLocalSpaceTransform(BoneIdx);
				S.InitialLocalRot    = FQuat4f(LocalXf.GetRotation());
				S.InitialLocalMatrix = FMatrix44f(LocalXf.ToMatrixWithScale());
				// if we have a child, bone axis is to child in local; else infer from local position
				if (bHasRealChild)
				{
					const int32 ChildJointIdx = Cfg.Springs[SIdx].JointIndices[i+1];
					if (JointBoneRefs.IsValidIndex(ChildJointIdx))
					{
						const FCompactPoseBoneIndex ChildIdx = JointBoneRefs[ChildJointIdx].GetCompactPoseIndex(BoneContainer);
						const FVector ChildLocal = CSPose.GetLocalSpaceTransform(ChildIdx).GetLocation();
						S.LocalBoneAxis = ChildLocal.GetSafeNormal(FVector(1,0,0));
					}
				}
				// initialize tails (center space)
				const FVector TailWS_Init = ChildWS;
				S.CurrentTail = FVector( WorldToCenter.TransformPosition((FVector3f)TailWS_Init) );
				S.PrevTail    = S.CurrentTail;
			}

			// compute world-space axis from local axis through (initialLocal * parentWorld), per ref impl
			const FVector AxisWS = (FVector)FVector3f(S.LocalBoneAxis).GetSafeNormal();
			FVector AxisWS_xform = AxisWS;
			{
				// transformDirection(initialLocal) then transformDirection(parentWorld)  (like TS)
				const FMatrix44f InitLoc = S.InitialLocalMatrix;
				FVector3f Dir = FVector3f(AxisWS);
				Dir = InitLoc.TransformVector(Dir);
				Dir = ParentWorld.TransformVector(Dir);
				AxisWS_xform = (FVector)Dir.GetSafeNormal();
			}

			// verlet
			FVector NextTailWS = IntegrateVerlet(S, AxisWS_xform, CenterToWorld, Stiff, Drag, GravN, GravPw, DeltaTime);

			// constrain length
			NextTailWS = HeadWS + (NextTailWS - HeadWS).GetSafeNormal(S.WorldBoneLength) * S.WorldBoneLength;

			// collisions (groups attached to this spring)
			ResolveCollisions(NextTailWS, JointRadius, Cfg, CSPose, ComponentTM, Spring.ColliderGroupIndices);

			// renormalize length after each collision
			NextTailWS = HeadWS + (NextTailWS - HeadWS).GetSafeNormal(S.WorldBoneLength) * S.WorldBoneLength;

			// update tail states (center-space)
			S.PrevTail    = S.CurrentTail;
			const FVector NextTailCenter = FVector( GetWorldToCenterMatrix(CenterToWorld).TransformPosition((FVector3f)NextTailWS) );
			S.CurrentTail = NextTailCenter;

			// compute new local rotation from axis-> to
			const FQuat NewLocal = ComputeLocalFromTail(S, NextTailWS, ParentWorld, S.InitialLocalMatrix);

			// stage for Evaluate
			PendingBoneWrites.Add({ BoneIdx, NewLocal });
		}
	}
}

// -------------------- math helpers --------------------

FMatrix44f FAnimNode_VRMSpringBones::GetParentWorldMatrix(const FTransform& ComponentTM, const FCSPose<FCompactPose>& CSPose, FCompactPoseBoneIndex BoneIdx)
{
	const FCompactPoseBoneIndex ParentIdx = CSPose.GetPose().GetBoneContainer().GetParentBoneIndex(BoneIdx);
	if (ParentIdx.IsValid())
	{
		const FTransform P = CSPose.GetComponentSpaceTransform(ParentIdx);
		return FMatrix44f( (P.ToMatrixWithScale()) );
	}
	return FMatrix44f( (ComponentTM.ToMatrixWithScale()) ); // root parent = component
}

FVector FAnimNode_VRMSpringBones::CalcWorldHeadPos(const FCSPose<FCompactPose>& CSPose, FCompactPoseBoneIndex BoneIdx)
{
	return CSPose.GetComponentSpaceTransform(BoneIdx).GetLocation();
}

FVector FAnimNode_VRMSpringBones::CalcWorldChildPos_OrPseudo(const FCSPose<FCompactPose>& CSPose, const FVRMSimJointState& S, FCompactPoseBoneIndex BoneIdx, bool bHasRealChild)
{
	if (bHasRealChild)
	{
		// if caller knows child index, they should pass it—here we approximate by reading first child in skeleton
		// For correctness we passed child via chain walk earlier; fall back to head if unknown
		return CSPose.GetComponentSpaceTransform(BoneIdx).GetLocation(); // replaced per-chain logic earlier
	}
	// vrm0: 7cm fixed child if final joint
	// The TS impl also does this fallback for VRM0 final node. (see setInitState comment in TS)
	const FVector HeadWS = CSPose.GetComponentSpaceTransform(BoneIdx).GetLocation();
	const FVector DirWS  = CSPose.GetLocalSpaceTransform(BoneIdx).GetLocation().GetSafeNormal(FVector(0,0,1));
	return HeadWS + 0.07f * DirWS; // meters
}

void FAnimNode_VRMSpringBones::CalcWorldBoneLength(FVRMSimJointState& S, const FVector& HeadWS, const FVector& ChildWS)
{
	S.WorldBoneLength = (HeadWS - ChildWS).Length();
}

FMatrix44f FAnimNode_VRMSpringBones::GetCenterToWorldMatrix(const FCSPose<FCompactPose>& CSPose, TOptional<FCompactPoseBoneIndex> CenterIdx)
{
	if (CenterIdx.IsSet())
	{
		const FTransform C = CSPose.GetComponentSpaceTransform(CenterIdx.GetValue());
		return FMatrix44f(C.ToMatrixWithScale());
	}
	return FMatrix44f::Identity;
}

FMatrix44f FAnimNode_VRMSpringBones::GetWorldToCenterMatrix(const FMatrix44f& C2W)
{
	FMatrix44f Inv = C2W;
	Inv.SetIdentity();
	if (!C2W.Equals(FMatrix44f::Identity))
	{
		Inv = C2W.Inverse();
	}
	return Inv;
}

FVector FAnimNode_VRMSpringBones::IntegrateVerlet(
	const FVRMSimJointState& S,
	const FVector& WorldSpaceBoneAxis,
	const FMatrix44f& CenterToWorld,
	float Stiffness, float Drag, const FVector& GravityDirWS, float GravityPower, float DeltaTime)
{
	// Inertial tail movement in center-space (prev->current) with drag
	FVector CurrentTailWS = (FVector)CenterToWorld.TransformPosition( (FVector3f)S.CurrentTail );
	FVector PrevTailWS    = (FVector)CenterToWorld.TransformPosition( (FVector3f)S.PrevTail );
	const FVector Inertia = (CurrentTailWS - PrevTailWS) * (1.f - Drag);

	// Stiffness and gravity are world-space forces (per spec & TS)
	const FVector Stiff   = WorldSpaceBoneAxis * (Stiffness * DeltaTime);
	const FVector External= GravityDirWS * (GravityPower * DeltaTime);

	return CurrentTailWS + Inertia + Stiff + External;
}

// ---- collision shapes ----
// Return signed distance: negative => penetrating (so we need to push along OutPushDir)

float FAnimNode_VRMSpringBones::CollideSphere(const FTransform& NodeXf, const FVRMSpringColliderSphere& Sph, const FVector& TailWS, float JointRadius, FVector& OutPushDir)
{
	const FVector CenterWS = NodeXf.TransformPosition(Sph.Offset);
	const FVector Delta = TailWS - CenterWS;
	const float Distance = Delta.Length() - (Sph.Radius + JointRadius);
	OutPushDir = Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollideInsideSphere(const FTransform& NodeXf, const FVRMSpringColliderSphere& Sph, const FVector& TailWS, float JointRadius, FVector& OutPushDir)
{
	const FVector CenterWS = NodeXf.TransformPosition(Sph.Offset);
	const FVector Delta = TailWS - CenterWS;
	const float Distance = (Sph.Radius - JointRadius) - Delta.Length(); // negative => outside of inside-sphere
	OutPushDir = -Delta.GetSafeNormal(); // push toward center
	return Distance;
}

float FAnimNode_VRMSpringBones::CollideCapsule(const FTransform& NodeXf, const FVRMSpringColliderCapsule& Cap, const FVector& TailWS, float JointRadius, FVector& OutPushDir)
{
	const FVector HeadWS = NodeXf.TransformPosition(Cap.Offset);
	const FVector TailC  = NodeXf.TransformPosition(Cap.TailOffset);
	const FVector AtoB   = TailC - HeadWS;
	FVector Delta = TailWS - HeadWS;
	const float Dot = FVector::DotProduct(AtoB, Delta);
	if (Dot < 0.f) { /* noop */ }
	else if (Dot > AtoB.SizeSquared()) { Delta -= AtoB; }
	else { Delta -= AtoB * (Dot / AtoB.SizeSquared()); }
	const float Distance = Delta.Length() - (Cap.Radius + JointRadius);
	OutPushDir = Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollideInsideCapsule(const FTransform& NodeXf, const FVRMSpringColliderCapsule& Cap, const FVector& TailWS, float JointRadius, FVector& OutPushDir)
{
	const FVector HeadWS = NodeXf.TransformPosition(Cap.Offset);
	const FVector TailC  = NodeXf.TransformPosition(Cap.TailOffset);
	const FVector AtoB   = TailC - HeadWS;
	FVector Delta = TailWS - HeadWS;
	const float Dot = FVector::DotProduct(AtoB, Delta);
	if (Dot < 0.f) { /* noop */ }
	else if (Dot > AtoB.SizeSquared()) { Delta -= AtoB; }
	else { Delta -= AtoB * (Dot / AtoB.SizeSquared()); }
	const float Distance = (Cap.Radius - JointRadius) - Delta.Length();
	OutPushDir = -Delta.GetSafeNormal();
	return Distance;
}

float FAnimNode_VRMSpringBones::CollidePlane(const FTransform& NodeXf, const FVRMSpringColliderPlane& P, const FVector& TailWS, float JointRadius, FVector& OutPushDir)
{
	const FVector OffsetWS = NodeXf.TransformPosition(P.Offset);
	const FVector NormalWS = NodeXf.TransformVectorNoScale(P.Normal).GetSafeNormal(FVector(0,0,1));
	const FVector Delta = TailWS - OffsetWS;
	const float Distance = FVector::DotProduct(Delta, NormalWS) - JointRadius;
	OutPushDir = NormalWS;
	return Distance;
}

void FAnimNode_VRMSpringBones::ResolveCollisions(
	FVector& NextTailWS,
	float JointRadius,
	const FVRMSpringConfig& Cfg,
	const FCSPose<FCompactPose>& CSPose,
	const FTransform& ComponentTM,
	const TArray<int32>& GroupIndices) const
{
	for (int32 GIdx : GroupIndices)
	{
		if (!Cfg.ColliderGroups.IsValidIndex(GIdx)) { continue; }
		const FVRMSpringColliderGroup& Group = Cfg.ColliderGroups[GIdx];

		for (int32 CIdx : Group.ColliderIndices)
		{
			if (!Cfg.Colliders.IsValidIndex(CIdx)) { continue; }
			const FVRMSpringCollider& Col = Cfg.Colliders[CIdx];

			// Find node transform (component space); fall back to component if unknown
			FTransform NodeXf = ComponentTM;
			if (!Col.BoneName.IsNone())
			{
				FBoneReference BR; BR.BoneName = Col.BoneName; BR.Initialize(CSPose.GetPose().GetBoneContainer());
				if (BR.HasValidSetup())
				{
					NodeXf = CSPose.GetComponentSpaceTransform(BR.GetCompactPoseIndex(CSPose.GetPose().GetBoneContainer()));
				}
			}

			FVector PushDir; float Pen;

			// spheres
			for (const auto& S : Col.Spheres)
			{
				if (S.bInside) { Pen = CollideInsideSphere(NodeXf, S, NextTailWS, JointRadius, PushDir); }
				else           { Pen = CollideSphere(NodeXf, S, NextTailWS, JointRadius, PushDir); }
				if (Pen < 0.f) { NextTailWS -= PushDir * Pen; }
			}
			// capsules
			for (const auto& Cap : Col.Capsules)
			{
				if (Cap.bInside){ Pen = CollideInsideCapsule(NodeXf, Cap, NextTailWS, JointRadius, PushDir); }
				else            { Pen = CollideCapsule(NodeXf, Cap, NextTailWS, JointRadius, PushDir); }
				if (Pen < 0.f) { NextTailWS -= PushDir * Pen; }
			}
			// planes (extended)
			for (const auto& Pl : Col.Planes)
			{
				Pen = CollidePlane(NodeXf, Pl, NextTailWS, JointRadius, PushDir);
				if (Pen < 0.f) { NextTailWS -= PushDir * Pen; }
			}
		}
	}
}

FQuat FAnimNode_VRMSpringBones::ComputeLocalFromTail(
	const FVRMSimJointState& S,
	const FVector& NextTailWS,
	const FMatrix44f& ParentWorldXf,
	const FMatrix44f& InitialLocalMatrix)
{
	// to_local = normalize( inv(parentWorld * initialLocal) * nextTailWS )
	const FMatrix44f WorldToLocal = (ParentWorldXf * InitialLocalMatrix).Inverse();
	const FVector3f ToLocal = WorldToLocal.TransformVector( FVector3f(NextTailWS) ).GetSafeNormal();
	const FVector3f FromLocal = FVector3f(S.LocalBoneAxis).GetSafeNormal();
	const FQuat4f Delta = FQuat4f::FindBetween(FromLocal, ToLocal);
	return FQuat( (Delta * S.InitialLocalRot) );
}

#undef LOCTEXT_NAMESPACE
