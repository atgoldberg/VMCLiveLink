# Task 08 — Rotation Write-Back to Parent Bones

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Convert simulated tips into **parent** bone local rotations; preserve translations; update the component-space pose once per frame after stepping.

## Dependencies
Tasks 01–07 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. For each joint:
   - `ParentPos` from component pose.
   - `SimDir = Normalize(CurrTip - ParentPos)`.
   - `AnimDir = Normalize(TipTargetAnim - ParentPos)`.
   - `DeltaCS = FQuat::FindBetweenNormals(AnimDir, SimDir)`.
2. Convert `DeltaCS` from component space to **parent local**; pre-multiply onto parent’s local rotation.
3. Write via `FComponentSpacePoseContext` helpers.

## Acceptance Criteria
- Visual check: parents aim at their tips; no translation/scale artifacts.
- Output pose remains stable and consistent at different frame rates.