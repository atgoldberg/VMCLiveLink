# Task 05 — Stiffness + Asset Gravity + Length Clamp

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Apply stiffness toward the animated target, add gravity from the asset (power + direction), and enforce fixed segment length each sub-step.

## Dependencies
Tasks 01–04 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. Compute `Accel = GravityDirectionFromAsset * GravityPowerFromAsset` (from SpringBoneData).
2. Stiffness as time-constant blend each sub-step:  
   `Alpha = 1 - exp(-Stiffness * h);`  
   `CurrTip = Lerp(CurrTip, TipTargetAnim, Alpha);`
3. Enforce segment length after stiffness:  
   `Dir = Normalize(CurrTip - ParentAnimatedPos);`  
   `CurrTip = ParentAnimatedPos + Dir * RestLength;`

## Acceptance Criteria
- Increasing Stiffness reduces target error monotonically without jitter.
- Gravity visibly affects sag/settle behavior per asset values.