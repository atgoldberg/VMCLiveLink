# Postmortem — Task 05: Stiffness + Asset Gravity + Length Clamp

## Summary
- **Goal:** Apply VRM spec spring forces (inertia, stiffness, gravity) and enforce segment length per sub-step using deterministic time stepping from Task 03 and Verlet state from Task 04.
- **Scope Implemented:**
  - Live component-space sampling of current joint (head) + parent rotation each frame / sub-step.
  - Additive stiffness force toward rest local bone axis, transformed by current parent/world rotation (spec-inspired formula).
  - External gravity force = `gravityDir * gravityPower` (asset driven) applied each sub-step.
  - Inertia (implicit velocity) with drag damping `(1 - Drag)`. 
  - Fixed-length constraint after each integration step (enforces `RestLength`).
  - Root?leaf solve order (array order validated in Task 02) preserved.
  - Guarded logging (non-shipping) of per-frame simulation summary.
- **Out of Scope (Deferred):** Collisions (Task 07), rotation write-back (Task 08), center-space evaluation (optional spec feature), limits/stretch (Task 09), debug draw (Task 10).

## Implementation Details
- **Files Modified:**  
  - `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp` — Added spec-conform additive forces, live component-space sampling, per-sub-step integration & constraint. Retained deterministic stepping parameters.
- **Key Types & Functions:**  
  - `FAnimNode_VRMSpringBone::Evaluate_AnyThread` — Executes sub-step loop; computes inertia, stiffness, gravity, re-normalizes segment length.  
  - `FVRMSBJointCache` — Uses `PrevTip`, `CurrTip`, `InitialLocalRotation`, `BoneAxisLocal`, and `RestLength` to derive spring forces.
- **Important Decisions:**  
  - Adopted additive stiffness (spec pseudo-code) instead of previous exponential blend for closer behavioral parity.  
  - Uses `ParentWorldRot.RotateVector(InitialLocalRotation.RotateVector(BoneAxisLocal))` to reconstruct rest direction each frame (accounts for animated upstream motion).  
  - Skips simulation for leaf joints (`RestLength <= small_number`) until a child (or terminal extension) exists—matching spec note on terminal joint usage.

## Spec & Data Conformance
- **VRM Spec References:**  
  - Inertia + stiffness + gravity + length constraint loop (SpringBone Algorithm / Inertia calculation section).  
  - Use of parent rotation and initial local orientation to derive stiffness target direction.  
  - Tail length constraint enforcement each step.
- **SpringBoneData Fields Used:**  
  - `Springs[].Stiffness`, `Springs[].Drag`, `Springs[].GravityDir`, `Springs[].GravityPower`, `Springs[].HitRadius` (stored for future collisions), `Joints[]` (bone indices for chain ordering).
- **Assumptions Avoided:**  
  - No new asset fields introduced; center-space and collider interaction deferred (per roadmap tasks).  
  - Did not approximate stiffness with blend; replaced earlier approach with additive form.

## Tests Performed
- **Manual (Editor):** Verified logs show consistent sub-step counts and chain/joint simulation; no allocations or crashes.  
- **Determinism Sanity:** Switching frame rate while using fixed step produced stable motion progression (observed via logging & tip position deltas).  
- **Edge Cases:** Springs with zero stiffness (pure inertia + gravity) and zero gravity (pure stiffness) behaved stably; drag 1.0 clamps movement (velocity null).  
- **Not Yet:** Automated tests (scheduled Task 12), visual pendulum scene (pending rotation write-back Task 08).

## Acceptance Criteria Mapping
- Additive stiffness force: Implemented (`StiffnessForce = RestDirCS * (Stiffness * h)`).
- Gravity power + direction from asset: Applied each sub-step (`ExternalGravity * h`).
- Inertia + drag: `Velocity = CurrTip - PrevTip`, scaled by `(1-Drag)` before integration.
- Length clamp after integration: Re-normalizes to `RestLength` each sub-step.
- Frame-rate independence: Fixed-step mode with accumulator; variable mode still deterministic per frame using uniform subdivision.

## Known Limitations & TODOs
- No collider push-out yet (Task 07) — tips can penetrate geometry; HitRadius unused.  
  *Mitigation:* Implement sphere & capsule resolution before enabling debug visuals.
- No rotation write-back (Task 08) — simulation invisible in output pose.  
- Center space (optional spec feature) unsupported; add transform remapping when Task 06/08 expands rotational context.  
- Stiffness scaling simplistic (no mass or h^2 factor) — acceptable baseline; revisit if tuning mismatch emerges.
- Leaf joints skipped; VRM0 auto tail extension not synthesized yet (optional enhancement).

## Handoff Notes
- **Next Task Impacted:** Task 06 (Root?Leaf propagation & rotation deferral) — joint ordering & per-joint rest data already provided; ensure any added rotation write-back happens after all sub-steps, not mid-loop.
- **Pre-Req for Task 07:** Collider shape caches are ready; implement collision after Verlet integration but before length re-constrain per sub-step (then re-apply final length clamp as spec).
- **Pre-Req for Task 08:** Rotation computation will use `(SimTip - HeadPos)` vs rest direction to build a from-to quaternion: `DeltaRot = FromTo(RestedDirLocal, CurrentDirLocal)` then apply to parent local rotation.

## Conclusion
Task 05 complete: Spec-intent forces (inertia, stiffness, gravity) and segment length constraint now integrated with deterministic sub-stepping. System ready for collider resolution and rotation write-back in subsequent tasks.
