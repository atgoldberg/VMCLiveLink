# Postmortem — Task 08: Rotation Write-Back to Parent Bones

## Summary
- **Goal:** Convert simulated spring tips into local parent bone rotations while preserving translations and scales, ensuring parents aim at simulated tips per VRM spec.
- **Scope Implemented:**
  - Post-simulation iteration over all chains/joints applying rotation deltas.
  - Component-space delta computation using `FQuat::FindBetweenNormals` between animated direction and simulated direction.
  - Conversion of component-space delta to parent-local space (grandparent frame) and pre-multiplication onto existing local rotation.
  - Safeguards for degenerate segments (zero length / invalid baseline vectors).
  - Logging tag `[VRMSpring][Task08]` for verbose per-frame confirmation (non-shipping).
  - Non-invasive integration: prior Tasks 01–07 logic preserved; rotation write-back appended after simulation.
- **Out of Scope (Deferred):**
  - Bone limit cones / twist limits (Task 09).
  - Debug draw of aimed axes (Task 10).
  - Live asset hot reload update triggers beyond existing cache rebuild (Task 11).

## Implementation Details
- **Files Added/Modified:**  
  - `AnimNode_VRMSpringBone.cpp` — Added Task 08 rotation write-back loop after simulation.
  - `postmortem-08.md` — This report.
- **Key Types & Functions:**  
  - `FAnimNode_VRMSpringBone::Evaluate_AnyThread` — Now performs: simulate (existing), then rotation write-back phase.
- **Important Decisions:**  
  - Use original pre-modification `CSPose` (captured before write-back) for computing `AnimDir` so stiffness targeting and rotation delta baseline remain stable and independent of previously rotated parents (avoids cascading drift).
  - Pre-multiply `LocalDelta * OldLocalRot` to rotate toward simulated tip while respecting existing pose modifications (mirrors conventional aim adjustment semantics).
  - Skip if `DeltaCS` is identity or degenerate to avoid unnecessary normalization/float noise.

## Spec & Data Conformance
- **VRM Spec References:**  
  - Spring bone orientation adjustment: align parent to child tip (VRM SpringBone spec behavior as seen in UniVRM implementation semantics).
- **SpringBoneData Fields Used:**  
  - `Springs[].JointIndices`, `Joints[].BoneName/NodeIndex/HitRadius`, `Springs[].HitRadius`, cached `RestLength`, `RestDirection` (from earlier tasks) to reconstruct fallback animated direction.
- **Assumptions Avoided:**  
  - No new asset fields introduced; no translation or scale edits; only rotation on parent bones with valid RestLength.

## Tests Performed
- **Unit/Automation:**  
  - (Existing low-level tests unchanged) Manual assertion checks on quaternion normalization in debug build.
- **Manual Scenes (from Global Test Plan):**  
  - Pendulum — Parent bone correctly aims; identical swing period at 30 vs 120 FPS (no divergence observed visually).
  - Hair — Multiple links rotate smoothly; no translation popping; colliders still effective (no regression from Task 07).
  - Tail/Scarf — Capsule collision preserved; tip aim correct; no scale artifacts.
  - Stress — ~100 joints w/ colliders: no additional allocations detected (steady memory), rotation phase linear in joint count.
- **Builds:**  
  - Editor / Standalone / Shipping — Shipping excludes verbose logs; rotation logic compiled (no editor-only guards needed).

## Acceptance Criteria Mapping
- Parents aim at simulated tips — Implemented via quaternion delta application.
- Only rotations modified — Translations/scales left untouched in `FTransform`.
- Stable frame-rate behavior — Simulation unchanged; rotations derived from final `CurrTip` after deterministic stepping.

## Known Limitations & TODOs
- Does not re-generate component-space pose after rotation updates (not required unless later stages depend on updated CSPose within same node; future tasks needing debug draw in component space may rebuild on demand).
- Leaf joints (RestLength == 0) skipped; acceptable per VRM semantics.
- Minor floating point jitter possible on nearly aligned vectors; could threshold angle magnitude for tighter filtering.

## Handoff Notes
- **Next Task Impacted:** Task 09 (Limits & Stretch) will clamp the computed `DeltaCS` before conversion or adjust `SimTip` beforehand; design choice: applying limits at tip position level preferred for consistency.
- **Suggested Follow-ups:** Optional smoothing of micro jitter (angle threshold), component pose recompute only if later tasks (debug drawing) require post-rotation component transforms.
