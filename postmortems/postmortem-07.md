# Postmortem — Task 07: Collider Resolution (Sphere + Capsule)

## Summary
- **Goal:** Integrate sphere & capsule collider resolution into the spring bone simulation per VRM spec: push tip spheres outward (or inward for inside colliders) after Verlet integration and before final length constraint reapplication. Subsequently, adjust integration formulas (stiffness, drag, gravity) to be frame-rate invariant and faithful to author intent from typical UniVRM-tuned parameters (per-frame semantics at 60 FPS).
- **Scope Implemented:**
  - World/component-space collider sampling each evaluation (no allocations beyond pre-sized arrays).
  - Support for sphere and capsule colliders with inside/outside semantics from data (`bInside`).
  - Per-substep, per-joint collision push-out integrated into existing root?leaf loop prior to final authoritative length clamp.
  - Zero-length capsules handled (degenerate to sphere behavior).
  - Deterministic stepping retained.
  - VRM compatibility scaling introduced: velocity damping, stiffness blend, and gravity displacement now scale using `s = (h * 60)` to emulate single-frame (60Hz) authored values across arbitrary sub-step counts.
  - Logging updated (`[Task07+Compat]`).

## Implementation Details
- **Files Modified:**
  - `AnimNode_VRMSpringBone.h` (earlier step): Added `bInside` to collider caches; world-space arrays for colliders.
  - `AnimNode_VRMSpringBone.cpp`:
    - Added `ClosestPointOnSegment` helper.
    - Built world-space collider caches each evaluation.
    - Inserted collision resolution inside simulation loop.
    - Replaced prior additive force model with frame-rate invariant mappings:
      - `s = h * 60` (fraction of 60 FPS frame represented by sub-step).
      - Velocity retention: `VelKeep = (1 - Drag)^s`.
      - Stiffness positional blend: `StiffAlpha = 1 - (1 - Stiffness)^s` then `NextTip = Lerp(NextTip, TargetTip, StiffAlpha)`.
      - Gravity displacement: `GravityDisplacement = GravityDir * GravityPower * s`.
    - Removed provisional pre-collision length clamp; only final clamp after collisions.
    - Preserved deterministic evaluation & no per-frame dynamic allocations beyond stable `SetNum`.

## VRM Authoring Compatibility Rationale
Typical VRM tools assume params applied once per 1/60s frame. Without scaling, increased substeps would amplify damping (Drag) and weaken stiffness, while gravity would under-apply per frame. The new exponent / scaling forms ensure multi-substep integration produces motion consistent with single-step reference behavior, preserving author tuning.

## Spec & Data Conformance
- **Data Fields Used:** Unchanged: collider offsets/radii/inside, joint hit radii, per-spring gravity, stiffness, drag.
- **No new fields** introduced; behavior purely derived from existing parameters + fixed reference frame rate (60 Hz).
- Simulation still component-space; collisions respect hit radii and inside semantics.

## Tests Performed (Manual)
1. Frame-rate invariance: Compared 1 substep @ 60 FPS vs 4 substeps @ 15 FPS (same total dt) — overlapping tip trajectories (visual + logged tip deltas within float noise).
2. Stiffness extremes: Stiffness=0 (pure inertia + gravity), Stiffness=1 (immediate snap each substep) — expected behaviors preserved independent of substep count.
3. Drag extremes: Drag=0 (full velocity), Drag=1 (velocity null) invariant across substeps.
4. Gravity consistency: Equal accumulated displacement across substep counts for identical real time.
5. Colliders: Verified tangent rest on spheres, sliding along capsules after scaling changes (no regressions).
6. Degenerate capsule: Still behaves as sphere.

## Acceptance Criteria Mapping (Original Task 07)
- Sphere & capsule resolution: Implemented.
- Inside/outside handling: Implemented.
- Deterministic order: Root?leaf, spheres then capsules, final length clamp.
- Allocation-free steady state: Achieved.

## Additional Acceptance (Compatibility Update)
- Frame-rate invariant behavior for stiffness, drag, gravity: Implemented via exponential mapping and scaled displacement.
- No change to asset schema: Confirmed.

## Known Limitations & TODOs
- `PrevTip` not corrected after collision push-out (may introduce minor kinetic artifacts). Potential improvement: set `PrevTip = CurrTip` if penetration occurred.
- No tangential friction; could project out normal component or add friction scalar later.
- Parent rotation not yet updated from simulation (Task 08) — stiffness uses animated parent orientation.
- Planes (present in data) ignored (not required yet).
- Extremely large hitch frames still clamped by `MaxDeltaTime`; visual smoothing of hitches out of scope.

## Handoff Notes
- Task 08 should compute parent rotations using post-collision, length-clamped `CurrTip` and rest direction.
- Debug drawing (Task 10) can visualize tip spheres (HitRadius), colliders, and rest directions for validation of compatibility scaling.
- If introducing optional non-compat mode later, guard formula selection; current mode implicitly VRM-compatible.

## Conclusion
Task 07 complete with added VRM parameter scaling improvements to preserve author intent across variable sub-stepping. System ready for rotation write-back (Task 08) without altering asset data or regressing collision or deterministic guarantees.
