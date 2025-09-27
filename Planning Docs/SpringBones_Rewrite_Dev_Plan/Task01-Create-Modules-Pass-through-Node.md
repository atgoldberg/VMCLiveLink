# Task 01 — Create Modules & Pass-through Node

> **Read first:** See `/VRM Spring Bones — Global Project State (Single Source of Truth).md` in the root of this pack for rules, sources of truth, and test plan.

## Goal
Create Runtime + Editor modules, an AnimGraph node (`FAnimNode_VRMSpringBones`) that passes through the input pose, and an editor wrapper (`UAnimGraphNode_VRMSpringBones`).

## Dependencies
None.

## Inputs
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (dev)
- **VRM Spec:** https://github.com/vrm-c/vrm-specification/tree/master/specification
- **Test Avatar:** `Renarde.vrmgltf`

## Do (Step-by-step)
1. Create modules `VRMSpringBonesRuntime` and `VRMSpringBonesEditor` (uplugin entries, Build.cs).
2. Implement `FAnimNode_VRMSpringBones` with overrides: `Initialize_AnyThread`, `CacheBones_AnyThread`, `Update_AnyThread`, `EvaluateComponentSpace_AnyThread`, `GatherDebugData`; ensure pass-through behavior.
3. Add `UPROPERTY` for `UVRMSpringBoneData* SpringData`.
4. Implement `UAnimGraphNode_VRMSpringBones` with title, category “VRM”, tooltip, node color.
5. Verify node appears in AnimGraph and logs one guarded line per tick.

## Acceptance Criteria
- Node compiles and forwards input pose unchanged.
- Plugin loads; editor node visible in palette.
- Logs confirm node executes each tick when placed in graph.