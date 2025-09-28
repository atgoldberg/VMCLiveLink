# Task: Fix Capsule Collider Algorithm for VRM Specification Compliance

## Objective
Update the capsule collider collision detection algorithm in `AnimNode_VRMSpringBone.cpp` to exactly match the VRM specification reference implementation.

## Current Issue
The current implementation in the `SimulateChains` method uses a simplified capsule collision algorithm that doesn't exactly match the VRM specification, particularly for the "between head and tail" case.

## Required Changes

### Location
File: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`
Method: `FAnimNode_VRMSpringBone::SimulateChains`

### Current Implementation (Line ~329)
```cpp
auto ResolveCapsule = [](const FVector& A, const FVector& B, float Radius, bool bInside, FVector& Tip, float TipRadius){
    // Current simplified implementation
};
```

### Required Implementation
Replace with the following spec-compliant implementation:

```cpp
auto ResolveCapsule = [](const FVector& A, const FVector& B, float Radius, bool bInside, FVector& Tip, float TipRadius){
    const float Combined = Radius + TipRadius;
    
    // Calculate offset to tail
    const FVector OffsetToTail = B - A;
    const FVector Delta = Tip - A;
    
    // Calculate dot product for position along capsule
    const float Dot = FVector::DotProduct(OffsetToTail, Delta);
    
    FVector AdjustedDelta = Delta;
    
    if (Dot < 0.0f) {
        // When the joint is at the head side of the capsule
        // Use delta as-is (no adjustment)
    } else if (Dot > OffsetToTail.SizeSquared()) {
        // When the joint is at the tail side of the capsule
        AdjustedDelta = Delta - OffsetToTail;
    } else {
        // When the joint is between the head and tail of the capsule
        AdjustedDelta = Delta - OffsetToTail * (Dot / OffsetToTail.SizeSquared());
    }
    
    const float DistSqr = AdjustedDelta.SizeSquared();
    if (DistSqr < KINDA_SMALL_NUMBER) {
        // Handle degenerate case
        FVector Axis = OffsetToTail.GetSafeNormal();
        if (Axis.IsNearlyZero()) Axis = FVector::UpVector;
        FVector Perp = FVector::CrossProduct(Axis, FVector::RightVector);
        if (Perp.IsNearlyZero()) Perp = FVector::CrossProduct(Axis, FVector::ForwardVector);
        Perp.Normalize();
        Tip = A + Perp * (bInside ? Combined * 0.99f : Combined + 0.1f);
        return;
    }
    
    const float Dist = FMath::Sqrt(DistSqr);
    const FVector Direction = AdjustedDelta / Dist;
    
    if (!bInside) {
        // Outside collider: push away if too close
        if (Dist < Combined) {
            const float Penetration = Combined - Dist;
            Tip += Direction * Penetration;
        }
    } else {
        // Inside collider: keep within bounds
        const float Inner = FMath::Max(0.f, Radius - TipRadius);
        if (Dist > Inner) {
            const float Penetration = Dist - Inner;
            Tip -= Direction * Penetration;
        }
    }
};
```

## Validation
1. Test with VRM models that have capsule colliders
2. Compare behavior with UniVRM reference implementation
3. Verify both outside and inside capsule colliders work correctly
4. Ensure no regression in sphere collider behavior

## Success Criteria
- Capsule collision detection matches VRM specification exactly
- No visual artifacts or unexpected behavior
- Performance remains comparable to previous implementation