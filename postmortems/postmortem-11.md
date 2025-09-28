# Postmortem — Task 11: Live Edits / Hot Reload

## Summary
Goal: Trigger spring cache rebuilds when a `UVRMSpringBoneData` asset is edited (PIE or reimport) with no frame?to?frame overhead and zero impact on shipping builds. Final approach: lightweight hash / revision change detection (no editor delegate retained in runtime module) plus improved parser compliance.

## Implementation Details
**Runtime Node (`AnimNode_VRMSpringBone.cpp`):**
- Removed earlier delegate approach (build environment constraint in runtime module). Rely on `UVRMSpringBoneData::EditRevision` + `SourceHash` via `GetEffectiveHash()`.
- On `CacheBones_AnyThread` and `Update_AnyThread`, compare stored `LastAssetHash` to `SpringConfig->GetEffectiveHash()`. Mismatch ? call `RebuildCaches_AnyThread` using required bones.
- Maintains zero work when unchanged (string compare only).

**Data Asset (`VRMSpringBoneData`):**
- `PostEditChangeProperty` bumps `EditRevision` for any tunable or Springs array change; clamps values & normalizes gravity direction.
- `GetEffectiveHash()` = `SourceHash + '_' + EditRevision` (cheap; structural JSON not re-hashed each edit).

**Parser (`VRMSpringBonesParser.cpp`):**
- Added parsing of `inside` boolean for both sphere and capsule shape variants (`sphere.inside`, `capsule.inside`, and type-based variants). Ensures inside colliders authored in VRM JSON are respected at runtime.

**Spec Compliance Adjustments:**
- Inside collider support now round?trips from JSON to runtime caches.
- Axis conversion for gravity unchanged (glTF down (0,-1,0) ? UE (0,0,-1)); documented in code.

## Acceptance Criteria Mapping
| Requirement | Implementation | Result |
|-------------|----------------|--------|
| Live edit rebuild | Hash mismatch triggers rebuild next update | Pass |
| No unsafe threading | Rebuild happens on anim thread; asset only read | Pass |
| No per-frame overhead when idle | Single string compare | Pass |
| Immediate effect for stiffness / gravity / collider edits | Edit bumps `EditRevision`; next frame rebuild | Pass |
| Shipping build unaffected | No `WITH_EDITOR` delegate logic required | Pass |

## Tests Performed
1. Adjust stiffness in PIE ? next frame logs rebuild (Verbose) and motion tightens.
2. Change collider radius & inside flag in asset ? contact behavior updates after single frame.
3. Reimport .vrm/.gltf file (different spring params) ? `SourceHash` changes; rebuild runs.
4. Idle test (no edits, 300 joints) ? no additional allocations (monitored with Memreport & stat anim). CPU cost unchanged vs Task 10 baseline.
5. Shipping configuration build ? no editor symbols; code path still functions (hash compare only).

## Risks / Considerations
- If a user edits deeply nested data without altering recognized tunable names (e.g., adding a new future field), `EditRevision` will not bump; acceptable for current scope.
- Full structural changes (adding/removing springs or joints) in editor are uncommon post-import; if performed, manual reimport recommended.
- Hash granularity: combining structural hash + edit revision would offer finer invalidation; current concat is sufficient.

## Future Enhancements (Optional)
- Expand `GetEffectiveHash()` to include a lightweight CRC of serialized Springs array for structural edits without reimport.
- Optional CVar to disable live rebuild for profiling (`vrm.Spring.LiveEdit 0/1`).
- Plane collider parsing/physics once spec finalizes (placeholder struct exists).
- Incremental rebuild (per-spring) for very large avatars.

## Conclusion
Task 11 requirements met with a simplified, low-risk approach; parser compliance improved (inside shapes). No regressions detected in tasks 01–10 behavior. Ready for integration.

