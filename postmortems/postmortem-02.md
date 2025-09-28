# Postmortem — Task 02: Cache SpringBoneData

## Summary
- **Goal:** Build immutable runtime caches (chains, joints, colliders) from `UVRMSpringBoneData` so Update/Evaluate allocate nothing.
- **Scope Implemented:**
  - Added runtime cache structures (chains, joints, collider shapes) inside anim node implementation (`FAnimNode_VRMSpringBone`).
  - Resolve bone references (BoneName or NodeIndex via `NodeToBoneMap`) to `FCompactPoseBoneIndex` in `CacheBones_AnyThread` via `RebuildCaches_AnyThread`.
  - Per-chain cache stores: spring params (stiffness, drag, gravity dir + power), ordered joints, flattened collider shape indices.
  - Per-joint cache stores: compact bone index, bone name (for ref skeleton lookups), effective hit radius, rest length/direction, and runtime tip buffers (`PrevTip`, `CurrTip`).
  - Colliders flattened into sphere & capsule arrays with bone binding and local offsets (no per-frame lookup of groups/colliders needed later).
  - Rest pose component-space transform building (single pass) and computation of joint?child rest direction & length; leaf joint tip initialized to its own position.
  - Change / hash detection using `UVRMSpringBoneData::GetEffectiveHash()` plus pointer swap detection to trigger cache rebuilds.
  - Guarded logging (non-shipping) summarizing rebuild counts and per-frame very-verbose evaluation status.
  - Strict pass-through pose evaluation preserved (simulation deferred to later tasks).

## Acceptance Criteria Mapping
| Criterion | Implementation Detail | Status |
|-----------|-----------------------|--------|
| Rebuild produces consistent chain/joint/collider counts | Counts taken directly from asset arrays; arrays reserved then Set/Append; logged summary | Met |
| No allocations during Update/Evaluate | All TArray allocations confined to rebuild path; Update/Evaluate only read arrays | Met |
| Bone & collider resolution performed in `CacheBones_AnyThread` | `RebuildCaches_AnyThread` invoked from `CacheBones_AnyThread` upon hash/pointer change | Met |
| Immutable caches (except simulation buffers) | Only `PrevTip/CurrTip` intended to mutate in future; currently initialized only once | Met |
| Support spheres & capsules | Both flattened into dedicated arrays with indices; capsules handled if present | Met |

## Implementation Notes
- Leaf Joint handling: Tips for leaf joints initialized to bone location (no child) to avoid uninitialized vectors; future solver will treat absence of child differently during propagation.
- Collider groups resolved once: Flattened shape index arrays stored per chain to avoid indirection cost later.
- Name + NodeIndex fallback: Prefers BoneName; falls back to NodeIndex map to ensure imported skeletal variations still resolve.
- Component-space rest pose: Built via manual parent accumulation (avoids additional utility calls) to get deterministic, allocation-free transforms.
- Logging kept lightweight (Log + VeryVerbose). No CVars yet (Task 10 will introduce them).

## Tests Performed
- Manual compile verification (build succeeded after adjustments).
- Asset absence (SpringConfig null): Node rebuild skipped gracefully.
- Hash change simulation: Incrementing `EditRevision` triggers rebuild next `CacheBones_AnyThread`.
- Diverse collider content: Verified code paths handle zero shapes, spheres-only, capsule inclusion.

## Risks / Follow-up
- Collider shape arrays currently retain all shapes (even invalid). Future tasks may skip invalid entries for minor iteration savings.
- RestDirection for leaf joints defaults forward; solver must handle terminal segments (Task 04+).
- No live edit listener yet (Task 11 will add editor notifications / refresh triggers).
- No instrumentation guard besides logging; profiling hooks can be added during Task 03/04 if needed.

## Handoff Notes
- Sub-stepping (Task 03) can now assume stable `ChainCaches` without further allocations.
- Verlet integration (Task 04) will update `PrevTip/CurrTip` only — safe to mutate.
- Gravity + stiffness (Task 05) will rely on cached RestLength/RestDirection and per-spring params.
- Collider resolution (Task 07) will iterate cached sphere / capsule arrays using indices stored per chain.
- Rotation write-back (Task 08) will require parent ? simulated tip orientation; parent bone component space positions can be sampled each frame and combined with cached tip states.

## No New Data Fields Added
All logic uses only existing fields from `VRMSpringBoneData.h` & spec; no speculative parameters introduced.

## Conclusion
Task 02 complete: immutable caches established with correct counts, zero per-frame allocations, and groundwork laid for subsequent deterministic simulation tasks.
