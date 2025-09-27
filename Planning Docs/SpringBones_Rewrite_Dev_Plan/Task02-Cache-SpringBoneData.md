# Task 02 — Cache SpringBoneData

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Resolve bone references and colliders from the data asset into immutable runtime caches; avoid any allocation during Update/Evaluate.

## Dependencies
Task 01 completed and merged.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. In `CacheBones_AnyThread`, resolve all `FBoneReference` to `FCompactPoseBoneIndex`.
2. Build `FChainCache` per chain with: bone indices; per-joint params; `RestDirection` (component space), `RestLength`, runtime buffers `PrevTip`/`CurrTip`.
3. Build `FColliderCache` for all colliders present in the asset (spheres and capsules if available), bind to their bone transforms.
4. Pre-reserve arrays; serialize sizes; expose light logging (guarded) on cache build.
5. Ensure `RebuildCaches_AnyThread` is called on initialization and when SpringData changes (later tasks will add live edit hooks).

## Acceptance Criteria
- Rebuild produces consistent chain/joint/collider counts matching the asset.
- No allocations occur during Update/Evaluate (verified via instrumentation/profiling).