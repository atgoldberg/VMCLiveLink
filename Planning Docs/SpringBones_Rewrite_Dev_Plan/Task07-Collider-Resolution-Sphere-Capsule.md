# Task 07 — Collider Resolution (Sphere + Capsule)

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Resolve penetration of tips against colliders defined in the asset; support sphere and capsule; optional normal damping for friction.

## Dependencies
Tasks 01–06 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. After stiffness/length, project each tip out of any penetrating colliders:
   - **Sphere:** push along normal by `(R - d)`.
   - **Capsule:** find closest point on segment; push along normal by `(R - d)`.
2. Optionally damp the normal component of tip velocity after projection to mimic friction.

## Acceptance Criteria
- Tips do not penetrate colliders in Hair and Tail/Scarf tests.
- No visible popping or tunneling with reasonable sub-steps.