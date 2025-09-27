# Task 11 — Live Edits and Hot Reload

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Rebuild caches when `UVRMSpringBoneData` properties change during PIE; thread-safe deferral.

## Dependencies
Tasks 01–10 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. Editor-only listener on asset changes triggers a reinit request for the node.
2. Caches rebuild on next tick without blocking or thread violations.

## Acceptance Criteria
- Editing stiffness/gravity/collider sizes updates behavior in PIE immediately.
- No crashes or race conditions observed.