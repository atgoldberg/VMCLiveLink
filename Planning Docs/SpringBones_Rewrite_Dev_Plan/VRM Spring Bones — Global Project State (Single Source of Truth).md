# VRM Spring Bones — Global Project State (Single Source of Truth)

## Project Goal
Implement a deterministic, component-space spring bone simulation node for **Unreal Engine 5.6** based on the **VRM specification**, driven by `UVRMSpringBoneData`. The node must simulate chains with stiffness, drag, **asset gravity (power + direction)**, and colliders, then write **parent bone rotations only** (no translations) in a Post-Process Anim Blueprint.

---

## Sources of Truth
- **Data Asset:** `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMSpringBoneData.h` (branch: `dev`).  
  • Only fields declared here are valid. Do **not** invent parameters.  
- **VRM Specification:** `Planning/VRM_SpringBones_Spec.md` and `Planning/VRM_SpringBones_Spec_Extracted.md` 
  • Spring bone behavior and collider semantics must follow this spec.  
- **Unreal Engine APIs:** Please refer to the files under `Engine/UE5` in this solution for all interfaces and helper functions.
- **Test Avatar:** `Renarde.vrmgltf` (import into a UE 5.6 project for validation).

---

## Core Rules
- **Spec Compliance:** Align behavior with the official VRM spec (link above).
- **Space:** Maintain simulation in **component space** until rotation write-back.
- **Parent Rotation Only:** Rotate the **parent bone** to aim at the simulated tip; never rotate the child to chase the tip.
- **Gravity:** Use gravity **power + direction from the asset**. No hardcoded defaults.
- **Time Stepping:** Deterministic sub-stepping; results must be frame-rate independent.
- **Colliders:** Support at least spheres; include capsules if provided by the asset.
- **Performance:** No per-frame allocations; caches rebuild only on reinit.
- **Debugging:** Toggle via CVars `vrm.Spring.Draw` and `vrm.Collider.Draw` (no cost when disabled).
- **Shipping Builds:** Exclude any debug rendering / editor-only code.

---

## Project Structure
**Plugin:** `VRMSpringBones`

- **Runtime Module (`VRMSpringBonesRuntime`)**
  - `FAnimNode_VRMSpringBones` (core node)
  - Chain & collider caches
  - Verlet + stiffness + gravity solver
  - Rotation write-back

- **Editor Module (`VRMSpringBonesEditor`)**
  - `UAnimGraphNode_VRMSpringBones` (editor node)
  - Debug draw (Editor/Dev only)
  - Hot reload for asset changes

---

## Current State (Baseline)
- Modules exist; load successfully.
- AnimNode exists; **pass-through pose**; logs ticks.
- Editor node visible under “VRM”.
- Caches/Solver/Write-back/Debug: **not implemented** yet.
- Tests: manual only.

> This baseline is the starting point for **Task 02** onward. Task 01 is considered created/merged if the pass-through stubs are already present; otherwise complete Task 01 first.

---

## Roadmap (Tasks 01–12)
Each task is **independent and self-contained**, but assumes prior tasks are merged. A new agent may start midstream by reading this doc.

1. **Create Modules & Pass-through Node**  
   Create Runtime + Editor modules; pass-through node + basic editor wrapper.
2. **Cache SpringBoneData**  
   Resolve bones/colliders from asset into immutable caches; no per-frame reallocs.
3. **Deterministic Sub-Stepping**  
   Implement fixed-delta sub-steps in `Update_AnyThread`.
4. **Verlet Core**  
   Integrate per-joint **tip** state (Prev/CurrTip) in component space.
5. **Stiffness + Asset Gravity + Length Clamp**  
   Blend toward animated target; apply asset gravity; enforce segment length.
6. **Root→Leaf Propagation**  
   Solve chains root→leaf each sub-step; defer rotations until after stepping.
7. **Collider Resolution (Sphere + Capsule)**  
   Push-out against colliders; optional normal damping (friction).
8. **Rotation Write-Back to Parent Bones**  
   Convert simulated tips to **parent** local rotations; preserve translations.
9. **Limits & Stretch (if present in asset)**  
   Clamp within cone/twist limits; optional small elasticity.
10. **Debug Draw & CVars**  
    Add `vrm.Spring.Draw` / `vrm.Collider.Draw` visualizations (Editor/Dev only).
11. **Live Edits / Hot Reload**  
    Rebuild caches when SpringBoneData changes during PIE.
12. **Test Plan & CI Sanity**  
    Manual scenes + basic automation for math helpers; Editor/Standalone/Shipping builds.

---

## Global Test Plan (applies to every task where relevant)
Validate using **Renarde.vrmgltf**:

1. **Pendulum:** Single chain; no colliders. Period stable at 30 vs 120 FPS.
2. **Hair:** Multi-link; head sphere collider. Sliding without penetration.
3. **Tail/Scarf:** Chains; body capsule collider. Stable under gravity + stiffness.
4. **Stress:** ≈100 simulated joints; ≥10 colliders. No GC churn; stable perf.

All must pass in **Editor**, **Standalone**, and **Shipping** builds (debug compiled out).

---

## Agent Workflow & Deliverables
1. **Read this Global Doc** before starting.
2. **Implement only your assigned task** as specified in its Task file (see `/tasks`).
3. **Cross-check** against:
   - `VRMSpringBoneData.h` (dev branch path above)
   - `Planning/VRM_SpringBones_Spec.md` and `Planning/VRM_SpringBones_Spec_Extracted.md`
   - `postmortems/postmortem-<previous-task-numbers>.md` (if applicable)
4. **Do not invent data fields.**
5. **Submit compiling code** against the baseline plugin.
6. **Produce a Post-Mortem file**: `/postmortems/postmortem-<task-number>.md` using the template below.

---

## Post-Mortem Requirement
Every task must include a post-mortem using this template: `/POSTMORTEM_TEMPLATE.md`. Place completed files in `/postmortems/`.

- Filename: `postmortem-<task-number>.md`
- Content: See template; must map implementation to acceptance criteria and list tests performed.

---