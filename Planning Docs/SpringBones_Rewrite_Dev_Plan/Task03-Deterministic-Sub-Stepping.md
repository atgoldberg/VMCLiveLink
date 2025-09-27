# Task 03 — Deterministic Sub-Stepping

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Implement frame-rate independent sub-stepping in `Update_AnyThread` with optional fixed delta override.

## Dependencies
Tasks 01–02 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. Compute `DeltaT = (FixedDeltaTime>0? FixedDeltaTime : Context.GetDeltaTime())`.
2. Clamp `Substeps` to [1..8]; compute `h = DeltaT / Substeps`.
3. Prepare sub-step loop; no allocations or cache rebuild here.

## Acceptance Criteria
- With the solver temporarily inert, logs show uniform sub-step behavior across FPS.
- Ready for integration steps without changes to public API.