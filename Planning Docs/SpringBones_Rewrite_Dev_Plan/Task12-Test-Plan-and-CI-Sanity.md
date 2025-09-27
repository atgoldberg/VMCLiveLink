# Task 12 — Test Plan and CI Sanity

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Implement manual scenes and basic automation for math helpers; verify Editor/Standalone/Shipping builds.

## Dependencies
Tasks 01–11 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. Manual scenes: Pendulum, Hair, Tail/Scarf, Stress (per Global Test Plan).
2. Automation: helper math (cone clamp, capsule distance, delta-quat).
3. CI/build scripts: ensure Shipping strips debug drawing; all three build modes succeed.

## Acceptance Criteria
- All four manual scenarios pass as described.
- Automation tests green; Editor/Standalone/Shipping builds succeed.