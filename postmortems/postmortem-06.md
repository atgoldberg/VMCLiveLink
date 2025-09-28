# Postmortem — Task 06: Root?Leaf Propagation

## Summary
- **Goal:** Reorder simulation to propagate spring bone tip positions root?leaf within each sub-step, using each simulated tip as the next joint’s anchor, while deferring any rotation write-back (kept for Task 08).
- **Scope Implemented:**
  - Outer loop over sub-steps, inner loop over joints (root?leaf) per chain.
  - Anchor selection: joint 0 uses animated joint (head) position; joint j>0 uses previous joint’s simulated `CurrTip` (fallback to animated head if parent invalid).
  - Per-joint integration (inertia + additive stiffness + gravity) unchanged from Task 05, now executed once per joint per sub-step.
  - Length constraint recomputed from the propagated anchor each sub-step.
  - Manual component-space pose accumulation (no allocations) to obtain current animated transforms.
  - Guarded verbose logging updated to indicate Task 06 propagation.
- **Out of Scope (Deferred):** Collisions (Task 07), rotation write-back (Task 08), center-space evaluation, limits/stretch (Task 09), debug visualization (Task 10).

## Implementation Details
- **Files Modified:**  
  - `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp` — Restructured evaluation loop for root?leaf per sub-step propagation; replaced helper call for CS pose fill with manual parent-first accumulation to resolve build issue; updated logging tag.
- **Key Types & Functions:**  
  - `FAnimNode_VRMSpringBone::Evaluate_AnyThread` — Executes nested (sub-step ? joints) loop, performs force integration and length clamp with propagated anchors.  
  - `FVRMSBJointCache` — Continues to store `PrevTip`, `CurrTip`, rest data, and local axis for stiffness direction derivation.
- **Important Decisions:**  
  - Moved sub-step loop outside joint loop to satisfy spec ordering (root ancestors affect descendants within the same sub-step).  
  - Used previous joint’s simulated tip directly (since rotations are not yet written back) to approximate post-rotation head position for the child.  
  - Deferred recomputation of parent rotations until Task 08; stiffness uses animated parent rotation (acceptable interim per roadmap).  
  - Manual CS pose build ensures no dependency on a signature-mismatched helper, avoiding regressions while remaining allocation free.

## Spec & Data Conformance
- **VRM Spec References:**  
  - SpringBone Algorithm: Calculation order (root to descendants), inertia + stiffness + gravity + length constraint loop.  
  - Use of ancestor-updated state influencing descendants within the same update step.
- **SpringBoneData Fields Used:**  
  - `Springs[].Stiffness`, `Springs[].Drag`, `Springs[].GravityDir`, `Springs[].GravityPower`, `Springs[].HitRadius` (stored), `Spring.JointIndices`, `Joints[].HitRadius`.
- **Assumptions Avoided:**  
  - No new fields introduced; no speculative center-space, collision, or mass parameters added.

## Tests Performed
- **Manual / Editor:** Multi-link chain shows expected distal lag vs proximal joints; no runtime allocations or crashes.
- **Determinism:** Fixed time-step mode produced identical tip progression across different frame rates (logging verified) — matches Task 03/04 guarantees.
- **Edge Cases:**
  - Single-joint (no child) chains correctly skipped (`RestLength <= small_number`).
  - Zero stiffness / zero gravity behave as inertial chain with damping.
  - Drag = 1.0 still freezes motion (unchanged). 
- **Builds:** Compiles after replacing prior CS pose fill call; no warnings added by Task 06 changes.

## Acceptance Criteria Mapping
- Root?leaf solve per sub-step: Implemented (outer sub-step loop, inner ascending joint loop).
- Use simulated tip as next joint anchor: Implemented (`AnchorPos = PrevJC.CurrTip`).
- No rotation write-back during stepping: Confirmed (no pose mutations besides cached tips).
- Stability of multi-link motion (distal lag): Observed manually; inertia and drag preserved.

## Known Limitations & TODOs
- No collision resolution yet; tips can penetrate colliders (Task 07 will insert push-out before final length re-clamp).
- Parent rotation not updated by simulation; children stiffness uses animated parent rotation until Task 08 (slight behavioral deviation vs full spec coupling).
- Manual CS pose accumulation assumes parent-first ordering in required bones (typical). If engine changes ordering, may revert to suitable runtime helper.
- No center-space option yet (deferred). 

## Handoff Notes
- **Next Task Impacted:** Task 07 — Insert collision resolution inside the inner joint loop after computing `NextTip` and before the final length constraint; re-apply length clamp after all collider push-outs.
- **Suggested Follow-ups:**  
  - Task 08: After all sub-steps, write parent bone rotations using simulated tip direction vs rest direction (component ? local conversion).  
  - Optionally restore engine helper for CS pose fill if signature alignment can be ensured, to reduce maintenance burden.

## Conclusion
Task 06 complete: Simulation now respects root?leaf dependency each sub-step using propagated simulated anchors, preserving prior tasks’ deterministic, spec-aligned forces without introducing new data fields or regressions. System ready for collider integration (Task 07) and rotation write-back (Task 08).
