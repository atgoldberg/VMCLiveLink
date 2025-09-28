# Post-Mortem: VRM SpringBone Capsule Collider Fix

**Task ID**: 01  
**Task Title**: Fix Capsule Collider Algorithm for VRM Specification Compliance  
**Developer**: GitHub Copilot  
**Date Completed**: 2025-09-28  
**Time Spent**: 1.0 hour

## Executive Summary
Replaced the simplified capsule collision resolver in `AnimNode_VRMSpringBone.cpp` with a VRM-spec-compliant implementation matching the UniVRM reference for head/tail/between cases and handling degenerate geometry. Change compiles cleanly and is localized to the capsule resolution lambda.

## Implementation Details

### Changes Made
| File | Change Type | Description |
|------|-------------|-------------|
| `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp` | Modified | Replaced `ResolveCapsule` lambda with VRM specification–compliant algorithm that projects the tip delta relative to capsule axis and applies outside/inside push logic. |

### Code Blocks
Key snippet (core logic added):

```cpp
// spec-compliant capsule resolver (core)
auto ResolveCapsule = [](const FVector& A, const FVector& B, float Radius, bool bInside, FVector& Tip, float TipRadius){
    const float Combined = Radius + TipRadius;
    const FVector OffsetToTail = B - A;
    const FVector Delta = Tip - A;
    const float Dot = FVector::DotProduct(OffsetToTail, Delta);
    FVector AdjustedDelta = Delta;
    const float OffsetToTailLenSqr = OffsetToTail.SizeSquared();

    if (Dot < 0.0f) {
        // head side: use Delta
    } else if (Dot > OffsetToTailLenSqr) {
        AdjustedDelta = Delta - OffsetToTail;
    } else {
        if (OffsetToTailLenSqr > KINDA_SMALL_NUMBER)
            AdjustedDelta = Delta - OffsetToTail * (Dot / OffsetToTailLenSqr);
    }

    // handle degenerate, compute distance, apply outside/inside push
    ...
};
```

### Algorithm/Logic Changes
1. Project the tip delta on the capsule axis per VRM spec to compute perpendicular component.  
2. Distinguish head / tail / between cases using dot product and axis squared length.  
3. Handle degenerate (near-zero perpendicular) with a stable perpendicular fallback.  
4. Apply outside (push out) and inside (keep within) logic using `Combined = Radius + TipRadius` and `Inner = max(0, Radius - TipRadius)`.

## Testing Performed

### Unit/Build
- [x] Code compiles successfully (build completed).  
- [ ] No automated unit tests added.

### Integration
- [ ] Runtime tests with VRM models not yet executed. Recommended: test with VRM models including inside/outside capsule cases and compare with UniVRM.

### Visual Validation
- [ ] Use debug draw flags `vrm.Spring.Draw` / `vrm.Collider.Draw` to visually inspect behavior during runtime.

## Challenges Encountered
- Degenerate capsule segment (zero-length) required robust fallback. Resolved by selecting a stable perpendicular axis and nudging the tip by a small epsilon.

## Deviations from Specification
- None intentional. Implementation follows the VRM spec reference for capsule resolution.

## VRM Specification Compliance
- Matches UniVRM reference implementation logic for capsule resolution (head/tail/between cases) in code. Runtime parity should be validated with test assets.

## Performance Impact
- No measurable impact expected; algorithm performs comparable vector math per tip as previous implementation.
- If profiling shows hotspots, consider micro-optimizations, but none were necessary for this change.

## Code Quality
- Uses `FVector`, `FMath`, and `KINDA_SMALL_NUMBER` to remain consistent with UE math practices.  
- No compiler errors or warnings introduced.

## Dependencies and Breaking Changes
- No new dependencies.  
- No breaking changes.

## Documentation Updates
- Consider adding an inline comment in the file referencing the VRM spec URL for future maintainers.

## Rollback Plan
1. Revert the commit that changed `AnimNode_VRMSpringBone.cpp`.  
2. Rebuild and validate previous behavior.

## Recommendations / Next Steps
1. Run runtime tests with several VRM models (inside/outside capsule) and verify behavior versus UniVRM.  
2. Enable collider debug draws for visual validation.  
3. Add an automated integration test or sample scene to catch regressions.

## Lessons Learned
### What Went Well
1. The VRM spec reference made the algorithm straightforward to implement.  
2. Build and compile validated the change quickly.

### What Could Be Improved
1. Add automated integration tests to validate behavior across assets.  
2. Add inline documentation linking to the VRM spec.

## Appendix
### Reference Materials
- VRM SpringBone specification — capsule reference implementation.  
- UniVRM reference implementation.

**Developer confirmation**:  
- [x] Implementation complete and compiled  
- [ ] Runtime behavioral tests pending

Signature: GitHub Copilot  
Date: 2025-09-28
