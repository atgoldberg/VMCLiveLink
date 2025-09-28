# Postmortem — Task 04: Verlet Core

## Summary
- **Goal:** Implement the deterministic Verlet core to advance per-joint tip state (Prev/Curr) in component space using the sub-step loop provided by Task 03. The core must be deterministic and frame-rate independent; higher-level forces (stiffness, gravity), length clamping, collisions and rotation write-back are deferred to later tasks.
- **Scope Implemented:**
  - Deterministic Verlet advancement using cached sub-step count (`CachedSubsteps`) and per-substep delta (`CachedH`).
  - Per-joint tip state (`PrevTip`, `CurrTip`) are advanced in component space with drag applied as damping to the implicit velocity.
  - No per-frame allocations; uses existing chain/joint caches.
  - Logging added (non-shipping builds) to verify simulation runs.

## Implementation Details
- **Files Modified:**
  - `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp` — added Verlet core advancement in `Evaluate_AnyThread` using cached `h` and `Substeps`. Removed earlier premature stiffness blending in favor of a pure Verlet step.

- **Key Types & Functions:**
  - `FVRMSBJointCache`: uses `PrevTip` and `CurrTip` to represent the two-step Verlet state for each joint.
  - `FAnimNode_VRMSpringBone::Evaluate_AnyThread`: now performs deterministic sub-step loop advancing tip states. Drag is applied as damping to the implicit velocity.

- **Design Decisions:**
  - External accelerations (gravity), stiffness toward the animated target, segment length enforcement, and collider resolution are intentionally left for Tasks 05–07 to keep Task 04 narrowly scoped to Verlet advancement.
  - The implementation keeps simulation in component space and does not perform any bone rotations or write-back (Task 08).
  - No changes were made to public APIs or the data asset. Placeholders for `FixedDeltaTime` and `Substeps` remain local to maintain compatibility with earlier tasks.

## Spec & Data Conformance
- Follows the reference Verlet integration pattern from the VRM spec: utilizes previous and current tail positions and advances using velocity (Curr - Prev). Drag (deceleration) is implemented as damping of the implicit velocity. Further forces and constraints will be added in subsequent tasks to match the full spec behavior.

## Tests Performed
- Manual verification in-editor: the code runs without allocating per-frame memory and logs simulation activity in non-shipping builds.
- Determinism check: using fixed delta overrides in `Update_AnyThread` (local placeholder) produces stable per-step behavior across frames (logging validated).

## Acceptance Criteria Mapping
- Deterministic sub-stepping loop integration: implemented and uses cached parameters from Task 03.
- Per-joint Prev/Curr tip state advanced by a Verlet step: implemented.
- No public API changes and no per-frame allocations: satisfied.

## Known Limitations & TODOs
- Stiffness, gravity (asset-driven), length clamps and collider handling are not yet implemented — reserved for Tasks 05–07.
- Rotation write-back (parent-to-child rotation update) is still pending (Task 08).
- `FixedDeltaTime` and `Substeps` remain local placeholders and should be exposed via the data asset or node UI in a future task if desired.

## Handoff Notes
- **Next Task:** Task 05 — Stiffness + Asset Gravity + Length Clamp. The deterministic stepped Verlet core and per-joint tip state are now available and will be used to apply explicit accelerations (gravity) and stiffness constraints per sub-step.
- **What to inspect for next task:** use `JC.PrevTip` and `JC.CurrTip` along with `CachedH` and `CachedSubsteps` to compute external accelerations and apply length clamping and stiffness per sub-step. Ensure all modifications remain in component space and preserve determinism.
