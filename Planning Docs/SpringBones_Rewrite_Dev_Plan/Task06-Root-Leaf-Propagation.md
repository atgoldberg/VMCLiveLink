# Task 06 — Root→Leaf Propagation

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Within each sub-step, solve joints from root to leaf and use each simulated tip as the next joint's anchor; defer write-back to after stepping.

## Dependencies
Tasks 01–05 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. For each chain and each sub-step: solve joint 0; then use its simulated tip as the anchor for joint 1, etc.
2. Do not write rotations during stepping; only cache the final simulated tips for Task 08.

## Acceptance Criteria
- Multi-link behavior is stable; distal links lag naturally.
- No mid-step writes to bone transforms.