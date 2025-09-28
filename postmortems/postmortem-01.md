# Postmortem — Task 01: Create Modules & Pass-through Node

## Summary
- **Goal:** Establish runtime + editor modules and a minimal VRM spring bones AnimGraph node that forwards the input pose unchanged and logs execution.
- **Scope Implemented:**
  - Runtime module `VRMSpringBonesRuntime` with `FAnimNode_VRMSpringBone` pass-through implementation.
  - Editor module `VRMSpringBonesEditor` with `UAnimGraphNode_VRMSpringBone` (palette entry: Category "VRM").
  - Single data asset pointer (`UVRMSpringBoneData* SpringConfig`) exposed on the node (wiring only; unused logic-wise yet).
  - Guarded per-frame log in non-shipping builds (VeryVerbose/Verbose depending on prior iteration) to confirm tick.
  - Debug data string added via `GatherDebugData`.
- **Out of Scope (Deferred):**
  - Bone / collider cache building (Task 02+).
  - Deterministic sub-stepping (Task 03).
  - Verlet integration, stiffness, gravity, length clamp (Tasks 04–05).
  - Root?leaf solve & rotation write-back (Tasks 06 & 08).
  - Collider resolution (Task 07).
  - Limits, stretch (Task 09).
  - Debug draw & CVars (Task 10).
  - Live edits / hot reload (Task 11).
  - Automated math/unit tests & CI scaffolding (Task 12).

## Implementation Details
- **Files Added/Modified:**  
  - `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Public/AnimNode_VRMSpringBone.h` — Defines minimal pass-through anim node with `SpringConfig` pointer and overrides.  
  - `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp` — Implements pass-through evaluation and guarded tick logging.  
  - `Plugins/VRMInterchange/Source/VRMSpringBonesEditor/Public/AnimGraphNode_VRMSpringBone.h/.cpp` (pre-existing) — Provides editor graph node title, tooltip, palette category.
- **Key Types & Functions:**  
  - `FAnimNode_VRMSpringBone` — Inherits `FAnimNode_Base`; overrides `Initialize_AnyThread`, `CacheBones_AnyThread`, `Update_AnyThread`, `Evaluate_AnyThread`, `GatherDebugData`. Currently only forwards pose.  
  - `UAnimGraphNode_VRMSpringBone` — Editor-facing wrapper exposing struct property and palette metadata.
- **Important Decisions:**  
  - Kept only a single data asset pointer (named `SpringConfig`) per existing upstream tooling expectation; future tasks may rename or dual-map to `SpringData` if required by spec wording.  
  - Logging limited to non-shipping builds to meet acceptance criteria without runtime overhead.

## Spec & Data Conformance
- **VRM Spec References:** None directly exercised (no simulation yet). Spec link acknowledged for future implementation (gravity, stiffness, colliders, parent-rotation rule).
- **SpringBoneData Fields Used:** Only the pointer type `UVRMSpringBoneData*`; no internal fields consumed in Task 01.
- **Assumptions Avoided:** Did not introduce any new parameters or guessed data fields; no speculative simulation variables retained.

## Tests Performed
- **Unit/Automation:** None (deferred; node logic trivial pass-through). Previous experimental tests were removed to align with minimal scope.
- **Manual Scenes (Global Test Plan):** Not applicable yet; node produces identical input/output pose (visually unchanged). Basic placement in an Anim Blueprint confirmed (palette visibility + log emission).
- **Builds:** Runtime/editor modules compile under UE 5.6 (Development). Shipping expected to compile (log compiled out) — to be re-validated in later tasks.

## Acceptance Criteria Mapping
- **Node compiles and forwards input pose unchanged:** Implemented; `Evaluate_AnyThread` only calls `ComponentPose.Evaluate`.
- **Plugin loads; editor node visible in palette:** Editor class present with category "VRM".
- **Logs confirm node executes each tick when placed in graph:** Guarded per-frame log added (non-shipping) with pass-through identifier.

## Known Limitations & TODOs
- No caching or validation of `SpringConfig` asset yet.
- No handling of missing / invalid asset beyond silent pass-through.
- No multi-thread specific optimizations or parallel-safe simulation structures (future tasks will add).
- Logging currently unconditional (per frame) in non-shipping; may be gated by future CVars once debug system arrives (Task 10).

## Handoff Notes
- **Next Task Impacted:** Task 02 (Cache SpringBoneData) will extend this node by adding internal chain & collider cache structures and rebuild triggers.
- **Suggested Follow-ups:**  
  - Add `SpringConfig` nullptr one-time warning (once caches matter).  
  - Introduce deterministic timing scaffolding (`AccumulatedTime`, sub-step logic) early in Task 03 to minimize churn.  
  - Decide final external property naming consistency (`SpringConfig` vs `SpringData`) before broad content adoption to avoid re-breaks in blueprints.
