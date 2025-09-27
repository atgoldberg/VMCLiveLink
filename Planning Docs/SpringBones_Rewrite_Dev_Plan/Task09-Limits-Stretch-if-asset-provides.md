# Task 09 — Limits & Stretch (if asset provides)

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
If present in the data asset, clamp within cone/twist limits and optionally allow small elasticity.

## Dependencies
Tasks 01–08 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. If the asset exposes limits, clamp `SimDir` inside a cone around `RestDirection` before the length clamp.
2. If elasticity is exposed, allow ±e% variation, else keep fixed length.

## Acceptance Criteria
- Extreme shaking stays within plausible arcs without snapping.
- When limits absent in asset, feature remains disabled (no-op).