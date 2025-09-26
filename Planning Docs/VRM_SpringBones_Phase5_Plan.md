# Incremental implementation plan for Phase 5 (Custom AnimGraph Node)

## Goal:
Add FAnimNode_VRMSpringBone + editor node with faithful VRM physics while minimizing regression risk. Deliver in small, shippable iterations with clear test gates and feature flags.

## Iteration Plan

### Iteration 0: Scaffolding (Compile-Only)
- Create runtime module: VRMSpringBonesRuntime
- Classes: FAnimNode_VRMSpringBone, UVRMSpringBoneAnimGraphNode (stub in editor module), UVRMSpringBoneSettings (optional transient UObject for overrides).
- Add build dependencies (AnimGraphRuntime, Engine, CoreUObject).
- Node passes input pose through unchanged. Test Gate: Compiles, template ABP updated to include node (still pass-through). Automation: ensure mesh pose unchanged.

### Iteration 1: Data Binding / Preprocessing
- At Initialize / CacheBones: resolve bone indices (FCompactPoseBoneIndex) from UVMRSpringBoneData FNames; store mapping + validity bitset.
- Precompute per-spring:
- Chain arrays (compact contiguous indices)
- Rest segment lengths (initial pose)
- Optional center bone index
- Add lightweight hash (SourceSignature) comparison to trigger full re-init if asset changed. Test Gate: Unit test loads synthetic skeleton + data asset; verifies chain lengths & bone resolution counts. Log summary (#Springs, #Joints, unresolved bones).

### Iteration 2: Core Integration Loop (No Collision)
- Implement Verlet or semi-implicit integration buffers: PreviousPos / CurrentPos per joint (Component-space vectors).
- Per-tick:
- On first update or Teleport: seed PreviousPos = CurrentPos.
- Apply stiffness toward animated pose endpoint (target bone tip).
- Enforce segment length (single forward pass).
- Write back only end-effectors (or all joints) as bone local rotations (derive from segment direction vs rest).
- Add ResetOnTeleport & bEnable flag. Test Gate: Unit test: static skeleton → stable. Animation with moving root → trailing lag appears. Determinism test (two runs same seed) equals.

### Iteration 3: Stiffness / Drag / Gravity / Center
- Add per-spring params: Stiffness, Drag (damping), GravityDir*Power (converted to component space), optional Center pull (offset clamp).
- Implement sub-stepping (configurable: fixed dt <= 1/90).
- Introduce FSpringRuntimeParams cached from data asset (flatten VRM 0.x vs 1.0 naming). Test Gate: Golden numeric test: feed scripted motion, assert position deltas sign (lag, gravity sag). Performance micro-benchmark (≤ X µs for N joints).

### Iteration 4: Sphere Collisions
- Build collider list (transform sphere centers each frame: bone transform + local offset).
- Per integration sub-step: after constraint, push joint outward if inside sphere (radius + jointRadius).
- Order: integrate → length constrain → collisions.
- Optional iteration count (≤2) per sub-step. Test Gate: Unit test: joint initialized inside sphere emerges at surface. Integration test: hair chain over shoulder sphere.

### Iteration 5: Capsule + Collider Groups
- Represent capsule as two spheres + radius; resolve by projecting point onto segment then push out.
- Group enable mask per spring (bitmask of collider groups).
- Optimize: Pre-filter colliders list per spring at Initialize using group membership. Test Gate: Capsule collision unit test, group filtering reduces tested collider count (assert counts).

### Iteration 6: Bone Space ↔ Component Space Robustness
- Support non-uniform parent transforms: store initial local rotations + rest directions.
- Rebuild CurrentPos from live animated pose each frame for root joints (allows partial simulation starting mid-chain).
- Add option: SimulateOnlyLeafBones vs WholeChain (perf toggle).
- Handle skeleton retarget replacements: revalidate indices if Skeleton GUID changes. Test Gate: Animation retarget scenario (replace skeleton mid-session) triggers safe reinit; no crash.

### Iteration 7: Parallel Evaluation / Thread Safety
- Move simulation to EvaluateComponentPose_AnyThread safe path:
- Gather inputs in PreUpdate (game thread), copy immutable runtime buffers.
- Allocate per-node scratch via FMemMark / TArray with preallocation.
- Introduce bAllowParallel + protect mutable state (double buffer Current/Previous positions).
- Add stats: STAT_VRMSpring_Update, per-joint cost (CSV profiler). Test Gate: Run with parallel animation enabled; race detector (ensure no writes to shared arrays). Frame-to-frame determinism intact.

### Iteration 8: Determinism / Stability Polish
- Fixed timestep accumulator (cap large DeltaTime; on hitch clamp or do multiple substeps).
- Add ResetIfBoneScaled detection (compare scale vs rest).
- CVar / settings:
- vrm.Spring.MaxSubSteps
- vrm.Spring.ClampHitchTime
- vrm.Spring.DebugDraw (0/1/2)
- Implement optional wind/global external force hook. Test Gate: Hitch simulation (inject 200ms) → no explosion (positions bounded). Determinism across runs.

### Iteration 9: Editor Node UX + Debug
- UAnimGraphNode_VRMSpringBone details:
- Asset picker (UVMRSpringBoneData soft ref)
- Overrides: GlobalStiffnessScale, GravityScale, Debug toggles
- Per-spring table (read-only summary)
- Debug draw (Editor & PIE):
- Lines for segments, spheres for colliders, color-coded constraint error
- Toggle via node + console variable Test Gate: PIE toggle real-time; enabling draw does not alter simulation results (checksum joints before/after with draw off/on).

### Iteration 10: Template ABP & Pipeline Integration
- Update template ABP: ComponentPose → VRMSpringBone Node → Output
- Pipeline (Phase 3) sets node’s SpringConfig reference post-duplication.
- Fallback: if asset missing / disabled, node auto PassThrough (no cost). Test Gate: Import VRM with/without spring data; ABP valid; missing asset path logs warning once and passes through.

### Iteration 11 (Optional Hardening)
- LOD awareness: if bone stripped at LOD, skip chain segments
- Perf auto-throttle: if last frame cost > threshold, reduce sub-steps next frame (soft adaptation)
- Metrics aggregation (avg ms, max error) exposed via editor window.

## Risk Mitigation / Feature Flags
- Global enable toggle: Project Settings + CVar gating runtime eval.
- Pass-through path always tested (Iteration 0 baseline).
- Collisions staged: spheres first (reduce complexity early).
- Parallelization deferred until core correctness proven.
- Determinism validated before adding adaptive perf features.

## Testing Strategy Summary
- Unit: Math (length preservation, collision resolve), parameter mapping, determinism, hitch handling.
- Integration: Imported VRM assets (0.x & 1.0), compare joint tip displacement ranges vs reference JSON expectations (tolerances).
- Performance: Synthetic stress (N springs × M joints) target budget; track in CSV profiling.
- Regression: Ensure disabling feature yields identical final pose hash vs baseline build.

## Instrumentation & Observability
- Scoped cycle counters per phase (Integrate, Constraint, Collision).
- Optional debug overlay: counts (Springs/Joints/SubSteps), ms, hitch warnings.
- Warning throttling for unresolved bones / invalid collider groups.

## Data Structure Highlights
- Per-spring struct: indices offset + count, params (packed), collider group mask (uint64), root & optional center indices.
- Per-joint runtime: CurrentPos, PrevPos (FVector3f), RestDir (FVector3f), RestLength (float).
- Colliders: SoA arrays for centers, radii, type enum, prefiltered index arrays per spring.

## Acceptance Criteria for Phase 5 Completion
1.	Node integrated & configurable; template ABP uses it automatically.
2.	Correct physical response (lag, stiffness, gravity) with no collisions producing believable motion.
3.	Sphere + capsule collisions functional & stable (no NaNs, no runaway energy).
4.	Deterministic across runs with identical inputs.
5.	Parallel animation compatible; no thread sanitizer warnings.
6.	Performance within target (documented baseline).
7.	Debug visualization & minimal editor UX present.
8.	All feature toggles off yields unchanged poses vs pre-Phase-5 importer.

This staged sequence allows shipping after Iteration 8 (core runtime) while continuing polish (debug/UI/perf) in subsequent iterations.