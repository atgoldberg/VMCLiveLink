# Postmortem — Task 10: Debug Draw & CVars (Updated After Spec Compliance + Micro Optimizations)

## Summary
Initial Task 10 delivered CVar-driven debug visualization. Subsequent reviews restored frame?rate invariant dynamics and added minor refinements: cached per-substep factors, gravity authoring note, and a small angular dead?zone to suppress micro jitter. No functional regressions introduced.

## Original Scope (Delivered)
- CVars: `vrm.Spring.Draw` (0=Off / 1=Basic / 2=Detailed) and `vrm.Collider.Draw` (0/1).
- Visuals (Editor/Dev only): chains, tips, rest vs sim directions, centers (mode 2), spheres + capsules (inside/outside tint).
- Shipping exclusion ensures zero cost in production.

## Enhancements After Review
| Change | Reason | Impact |
|--------|--------|--------|
| Exponential drag & stiffness mapping (`(1-x)^(h*60)`) | Restore frame-rate invariance | Consistent motion across dt/substeps |
| Single final length clamp | Align with typical VRM flows | Cleaner collision response |
| Cached per-substep factors (drag retain, stiffness alpha) | Micro optimization | Reduces pow() calls per joint |
| Gravity scaled by `h*60` with code comment | Clarify authored-at-60Hz semantics | Predictable designer tuning |
| NaN/Inf guard (component finite + `ContainsNaN`) | Stability | Prevents corrupt state propagation |
| Angular dead-zone (0.5°) on rotation write-back | Suppress microscopic jitter & noise | Visual stability without perceptible lag |

## Dead-Zone Detail
- Threshold: 0.5 degrees (`kRotationDeadZoneRad = 0.5 * PI/180`).
- Skip rotation update when delta angle below threshold; pose remains last valid orientation.
- Weight blending still applies for larger rotations; no accumulation drift since aiming is stateless per frame.

## Performance Notes
- Moved `Pow` evaluations for drag & stiffness outside joint loop per substep: O(substeps + joints) vs previous O(substeps * joints).
- Dead-zone early continue avoids Slerp on negligible changes (common in settled chains).
- No additional allocations.

## Tests (Incremental)
| Test | Result |
|------|--------|
| Pendulum 30 vs 120 FPS | Identical period/amplitude (epsilon variance) |
| Hair collider slide | No penetration; reduced micro jitter at rest |
| High substeps (8) | Motion matches substeps=1 baseline visually |
| Zero-weight toggle | Seamless resume; no pop |
| Edge NaN injection (forced) | Guard resets safely to rest length |

## Acceptance Criteria Mapping
| Criterion | Status |
|----------|--------|
| Debug CVars functional & isolated | Pass |
| No shipping overhead | Pass |
| Accurate chain & collider visualization | Pass |
| Frame-rate independent dynamics | Pass (reconfirmed) |
| Minimal jitter at rest | Improved via dead-zone |
| No new asset fields introduced | Pass |

## Limitations / Future Options
- Dead-zone value fixed; could be exposed as an advanced CVar if artist tweaking becomes necessary.
- Gravity normalization assumes 60Hz authoring; an alternative canonical frame rate could be parameterized later.
- No tangent/frictional collision response yet (optional enhancement).
- Limits & elasticity remain unimplemented pending asset specification fields.

## Conclusion
Task 10 remains compliant with spec goals after minor optimizations and stability improvements. The system now exhibits stable, deterministic, low-noise motion consistent with VRM reference behavior while retaining a clean debug pathway.
