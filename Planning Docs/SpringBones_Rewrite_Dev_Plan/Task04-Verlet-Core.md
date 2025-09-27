# Task 04 — Verlet Core

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Integrate a tip point per joint in component space using classic Verlet buffers.

## Dependencies
Tasks 01–03 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. For each joint, define animated target tip per frame: `TipTargetAnim = ParentAnimatedPos + RestDirection * RestLength`.
2. Maintain per-joint `PrevTip` and `CurrTip` arrays in component space.
3. Per sub-step:  
   `Velocity = (CurrTip - PrevTip) * (1 - Drag);`  
   `NextTip  = CurrTip + Velocity + Accel * h*h;` (Accel initially zero; gravity will be added in Task 05)  
   `PrevTip  = CurrTip; CurrTip = NextTip;`

## Acceptance Criteria
- With Drag=1 → tips freeze; Drag<1 → velocity decays smoothly.
- No local-space math; all in component space.