# Task 10 — Debug Draw & CVars

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Implement editor/dev-only visualization toggled at runtime with CVars.

## Dependencies
Tasks 01–09 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. Add CVars: `vrm.Spring.Draw` (0/1/2) and `vrm.Collider.Draw` (0/1).
2. Draw chains/tips/axes and colliders using `DrawDebug*` or a debug proxy.
3. Compile out in Shipping; ensure zero cost when disabled.

## Acceptance Criteria
- Toggling CVars at runtime shows/hides visuals instantly.
- No measurable overhead when disabled; excluded from Shipping builds.