# Postmortem — Task 09: Limits & Stretch (Spec Alignment, Placeholder, Blend Weight & Optimization)

## Summary
Extended Task 09 implementation with a performance optimization: when `Weight == 0` the node now fully bypasses simulation scheduling and evaluation (pure pass-through) while preserving prior spec-aligned behavior for any non-zero weight. Limits and elasticity remain placeholders (no asset fields). Core simulation (for Weight > 0) matches VRMC_springBone reference implementations.

## Additions Since Previous Revision
- Weight==0 optimization in `Update_AnyThread` (skips substep scheduling; accumulator reset) and in `Evaluate_AnyThread` (early return, no CSPose building or collision work).
- Logging updated to indicate skip state at VeryVerbose level.
- No change to blend logic for partial weights (0 < Weight < 1) or seeding on activation.

## Current Functional Flow
1. Update phase: if asset invalid -> pass-through. If `Weight <= 0` -> set `CachedSubsteps=0` and exit (no time accumulation). Else deterministic fixed/variable substep scheduling.
2. Evaluate phase:
   - If `Weight <= 0`: pass-through (no CSPose, no simulation, no rotations).
   - If weight activated this frame (transition 0 -> >0): re-seed tips from animated child positions.
   - Perform spec-aligned Verlet integration, collisions, strict length constraint.
   - Rotation write-back: `InitialLocalRotation * FromTo(BoneAxisLocal, SimDirLocal)` then Slerp with animated local by `Weight`.

## Performance Impact
- Skipping simulation when Weight=0 avoids per-frame CSPose construction, collider world transform computation, loops over joints, and memory SetNum operations. Only the input pose evaluation remains.
- Accumulator reset ensures clean deterministic start when weight increases again.

## Edge Cases Considered
- Rapid toggling weight 0?1: Re-seed prevents snapping; deterministic restart due to cleared accumulator.
- Weight tiny but >0 (e.g., 0.001): still simulates fully and blends almost entirely to animation—documented expected cost; users should clamp to exactly 0 for maximum savings.
- Chains with zero joints or leaf-only entries gracefully skipped as before.

## Compliance & Parity (Tasks 01–09)
Unchanged from prior assessment except for optimization path:
- All implemented features remain spec-consistent for VRM 1.0 fields: inertia, stiffness, drag, gravity, collision, length constraint, center space, parent rotation rebuild.
- Limits/elasticity: still pending future asset fields (placeholders present).

## Tests (Manual)
1. Weight=0 (Idle): CPU usage lowered; logs show skipped path; no allocations from spring arrays (monitored via Unreal stats snapshot).
2. Toggle Weight 0?1 mid-animation: Smooth fade-in, no pop.
3. Partial Weight 0.5: Identical visual result as before optimization.
4. Stress (100 joints) Weight=0: negligible overhead vs disabled node baseline.
5. Stress fade test (0 for 2s, then 1): Stable; no drift or jump.

## Risks / Mitigations
- Tip state is not advanced while Weight=0; when reactivated, state is re-seeded from animation, producing an abrupt change only if the designer expected hidden simulation to continue. Documented behavior; matches common Unreal node conventions.
- If future limits rely on simulation continuity while faded out, may need optional background sim toggle.

## Future Enhancements
- Optional bool `bSimulateWhenZeroWeight` to keep state warm (not implemented yet).
- CVar to force simulation even at zero weight for debugging comparisons.
- Telemetry counters for skipped frames vs active frames (for profiling larger scenes).

## Conclusion
Task 09 now includes both blend and zero-weight optimization, delivering spec-parity motion with scalable cost. The system remains ready for Task 10 debug visualization and future extension fields without refactoring.
