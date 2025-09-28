# Postmortem — Task 03: Deterministic Sub-Stepping

## Summary
- **Goal:** Implement deterministic, frame-rate independent sub-stepping in `Update_AnyThread` with optional fixed-delta override and prepare for later solver integration.
- **Scope Implemented:**
  - Compute frame delta with optional fixed-delta override and clamp substeps to [1..8].
  - Compute per-substep delta `h = DeltaT / Substeps`.
  - Add a deterministic sub-step loop in `Update_AnyThread` with no allocations.
  - Log sub-step parameters for verification while solver remains inert.
- **Out of Scope (Deferred):** Actual solver integration (Verlet advance), stiffness/drag, gravity, collision resolution, and rotation write-back (reserved for subsequent tasks).

## Implementation Details
- **Files Added/Modified:**
  - `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp` — implemented deterministic sub-stepping logic and logging inside `Update_AnyThread`.
  - `postmortems/postmortem-03.md` — this postmortem documenting the task.
- **Key Types & Functions:**
  - `FAnimNode_VRMSpringBone::Update_AnyThread` — now computes `DeltaT`, clamps `Substeps`, computes `h`, logs values, and iterates a deterministic sub-step loop (solver inert).
- **Important Decisions:**
  - No new public API was introduced. Asset-level overrides for fixed delta and substeps are wired as local placeholders to avoid inventing fields; future tasks can expose these via the data asset if needed.
  - Logging is gated to non-shipping builds only.

## Spec & Data Conformance
- **VRM Spec References:**
  - Time-stepping requirement from the global project doc: "Deterministic sub-stepping; results must be frame-rate independent." (Planning Docs/VRM Spring Bones — Global Project State)
- **SpringBoneData Fields Used:**
  - `UVRMSpringBoneData::SpringConfig` (existing) to detect validity and prevent work when no asset is present.
- **Assumptions Avoided:**
  - No new fields were added to `UVRMSpringBoneData`; placeholder local variables are used for `FixedDeltaTime` and `Substeps` to avoid inventing asset fields.

## Tests Performed
- **Unit/Automation:**
  - N/A (no math helpers added yet).
- **Manual Scenes:**
  - Pendulum / Hair / Tail / Stress tests deferred until solver integration.
- **Builds:**
  - Project builds locally after changes (compile validated in-editor environment).

## Acceptance Criteria Mapping
- **With the solver temporarily inert, logs show uniform sub-step behavior across FPS:**
  - Implemented logging of `DeltaT`, `Substeps`, and `h` per frame in non-shipping builds.
- **Ready for integration steps without changes to public API:**
  - Sub-step loop and deterministic `h` computation are in place; solver integration can proceed in subsequent tasks without public API changes.

## Known Limitations & TODOs
- FixedDeltaTime and Substeps are currently local placeholders and not exposed to the data asset or node UI. Future task should add these fields to the asset or node if required.
- Actual simulation (Verlet stepping, stiffness, gravity, collisions, write-back) remains to be implemented in Tasks 04–08.

## Handoff Notes
- **Next Task Impacted:** Task 04 — Verlet Core. This implementation provides the deterministic stepping loop and per-step delta necessary for correct Verlet advancement.
- **Suggested Follow-ups:**
  - Add asset-driven or node-driven overrides for `FixedDeltaTime` and `Substeps` if desired.
  - Implement per-joint Verlet integration using `ChainCaches[].Joints[].PrevTip/CurrTip` and `h` computed here.

