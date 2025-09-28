# Task: Optimize Collision Shape Caching and Memory Management

## Objective
Optimize collision shape world-space caching to reduce per-frame allocations and improve cache performance.

## Current Issue
The current implementation resizes collision cache arrays every frame and doesn't pre-allocate capacity during initialization, leading to unnecessary allocations and potential cache misses.

## Required Changes

### 1. Pre-allocate Cache Arrays with Reserved Capacity

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`

Modify the `RebuildCaches_AnyThread` method to reserve capacity:

```cpp
void FAnimNode_VRMSpringBone::RebuildCaches_AnyThread(const FBoneContainer& BoneContainer)
{
    InvalidateCaches();
    if (!SpringConfig || !SpringConfig->SpringConfig.IsValid()) return;
    
    const FVRMSpringConfig& Data = SpringConfig->SpringConfig;
    
    // Calculate total counts for reservation
    int32 TotalSphereShapes = 0, TotalCapsuleShapes = 0, TotalPlaneShapes = 0;
    for (const FVRMSpringCollider& Collider : Data.Colliders)
    { 
        TotalSphereShapes += Collider.Spheres.Num(); 
        TotalCapsuleShapes += Collider.Capsules.Num(); 
        TotalPlaneShapes += Collider.Planes.Num(); 
    }
    
    // Reserve shape cache capacity
    SphereShapeCaches.Reserve(TotalSphereShapes);
    CapsuleShapeCaches.Reserve(TotalCapsuleShapes);
    PlaneShapeCaches.Reserve(TotalPlaneShapes);
    
    // NEW: Pre-allocate world position caches with reserved capacity
    SphereWorldPos.Reserve(TotalSphereShapes);
    SphereWorldPos.SetNum(TotalSphereShapes, false); // Don't shrink
    
    CapsuleWorldP0.Reserve(TotalCapsuleShapes);
    CapsuleWorldP0.SetNum(TotalCapsuleShapes, false);
    CapsuleWorldP1.Reserve(TotalCapsuleShapes);
    CapsuleWorldP1.SetNum(TotalCapsuleShapes, false);
    
    PlaneWorldPoint.Reserve(TotalPlaneShapes);
    PlaneWorldPoint.SetNum(TotalPlaneShapes, false);
    PlaneWorldNormal.Reserve(TotalPlaneShapes);
    PlaneWorldNormal.SetNum(TotalPlaneShapes, false);
    
    // ... rest of existing cache building code ...
}
```

### 2. Optimize PrepareColliderWorldCaches Method

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`

Replace the current implementation with an optimized version:

```cpp
void FAnimNode_VRMSpringBone::PrepareColliderWorldCaches(FCSPose<FCompactPose>& CSPose)
{
    // Use SetNumUninitialized for better performance (no zeroing)
    // Arrays are already pre-allocated with correct capacity
    const int32 NumSpheres = SphereShapeCaches.Num();
    const int32 NumCapsules = CapsuleShapeCaches.Num();
    const int32 NumPlanes = PlaneShapeCaches.Num();
    
    // Ensure sizes match (should already be correct from RebuildCaches)
    if (SphereWorldPos.Num() != NumSpheres) {
        SphereWorldPos.SetNumUninitialized(NumSpheres, false);
    }
    if (CapsuleWorldP0.Num() != NumCapsules) {
        CapsuleWorldP0.SetNumUninitialized(NumCapsules, false);
        CapsuleWorldP1.SetNumUninitialized(NumCapsules, false);
    }
    if (PlaneWorldPoint.Num() != NumPlanes) {
        PlaneWorldPoint.SetNumUninitialized(NumPlanes, false);
        PlaneWorldNormal.SetNumUninitialized(NumPlanes, false);
    }
    
    // Batch process sphere transforms
    for (int32 i = 0; i < NumSpheres; ++i)
    {
        const FVRMSBSphereShapeCache& SC = SphereShapeCaches[i];
        if (!SC.bValid || !SC.BoneIndex.IsValid()) {
            SphereWorldPos[i] = FVector::ZeroVector;
            continue;
        }
        
        const FTransform& BoneCST = CSPose.GetComponentSpaceTransform(SC.BoneIndex);
        SphereWorldPos[i] = BoneCST.TransformPosition(SC.LocalOffset);
    }
    
    // Batch process capsule transforms
    for (int32 i = 0; i < NumCapsules; ++i)
    {
        const FVRMSBCapsuleShapeCache& CC = CapsuleShapeCaches[i];
        if (!CC.bValid || !CC.BoneIndex.IsValid()) {
            CapsuleWorldP0[i] = CapsuleWorldP1[i] = FVector::ZeroVector;
            continue;
        }
        
        const FTransform& BoneCST = CSPose.GetComponentSpaceTransform(CC.BoneIndex);
        CapsuleWorldP0[i] = BoneCST.TransformPosition(CC.LocalP0);
        CapsuleWorldP1[i] = BoneCST.TransformPosition(CC.LocalP1);
    }
    
    // Batch process plane transforms
    for (int32 i = 0; i < NumPlanes; ++i)
    {
        const FVRMSBPlaneShapeCache& PC = PlaneShapeCaches[i];
        if (!PC.bValid || !PC.BoneIndex.IsValid()) {
            PlaneWorldPoint[i] = FVector::ZeroVector;
            PlaneWorldNormal[i] = FVector::UpVector;
            continue;
        }
        
        const FTransform& BoneCST = CSPose.GetComponentSpaceTransform(PC.BoneIndex);
        PlaneWorldPoint[i] = BoneCST.TransformPosition(PC.LocalPoint);
        PlaneWorldNormal[i] = BoneCST.TransformVectorNoScale(PC.LocalNormal).GetSafeNormal();
        
        if (PlaneWorldNormal[i].IsNearlyZero()) {
            PlaneWorldNormal[i] = FVector::UpVector;
        }
    }
}
```

### 3. Add Memory Pool for Temporary Calculations

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Public/AnimNode_VRMSpringBone.h`

Add member variables for calculation pools:

```cpp
private:
    // ... existing members ...
    
    // Memory pools for temporary calculations
    mutable TArray<FVector> TempVectorPool;
    mutable TArray<float> TempFloatPool;
    mutable int32 TempVectorPoolIndex = 0;
    mutable int32 TempFloatPoolIndex = 0;
    
    // Pool management
    void ResetTempPools();
    FVector* GetTempVector();
    float* GetTempFloat();
```

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`

Add pool management implementation:

```cpp
void FAnimNode_VRMSpringBone::ResetTempPools()
{
    TempVectorPoolIndex = 0;
    TempFloatPoolIndex = 0;
    
    // Ensure pools have sufficient capacity
    const int32 ExpectedVectors = TotalValidJoints * 4; // Estimate
    const int32 ExpectedFloats = TotalValidJoints * 8;
    
    if (TempVectorPool.Num() < ExpectedVectors) {
        TempVectorPool.SetNumUninitialized(ExpectedVectors, false);
    }
    if (TempFloatPool.Num() < ExpectedFloats) {
        TempFloatPool.SetNumUninitialized(ExpectedFloats, false);
    }
}

FVector* FAnimNode_VRMSpringBone::GetTempVector()
{
    if (TempVectorPoolIndex >= TempVectorPool.Num()) {
        TempVectorPool.Add(FVector::ZeroVector);
    }
    return &TempVectorPool[TempVectorPoolIndex++];
}

float* FAnimNode_VRMSpringBone::GetTempFloat()
{
    if (TempFloatPoolIndex >= TempFloatPool.Num()) {
        TempFloatPool.Add(0.0f);
    }
    return &TempFloatPool[TempFloatPoolIndex++];
}

// Use in SimulateChains at the beginning
void FAnimNode_VRMSpringBone::SimulateChains(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& CSPose)
{
    ResetTempPools(); // Reset pools for this frame
    // ... rest of simulation code ...
}
```

### 4. Cache-Friendly Data Layout

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Public/AnimNode_VRMSpringBone.h`

Consider restructuring collision data for better cache locality:

```cpp
// Structure-of-Arrays for better cache performance
struct FColliderSOA
{
    // Sphere data
    TArray<FVector> SpherePositions;
    TArray<float> SphereRadii;
    TArray<bool> SphereInside;
    
    // Capsule data  
    TArray<FVector> CapsuleP0;
    TArray<FVector> CapsuleP1;
    TArray<float> CapsuleRadii;
    TArray<bool> CapsuleInside;
    
    // Plane data
    TArray<FVector> PlanePoints;
    TArray<FVector> PlaneNormals;
    
    void Reserve(int32 NumSpheres, int32 NumCapsules, int32 NumPlanes)
    {
        SpherePositions.Reserve(NumSpheres);
        SphereRadii.Reserve(NumSpheres);
        SphereInside.Reserve(NumSpheres);
        
        CapsuleP0.Reserve(NumCapsules);
        CapsuleP1.Reserve(NumCapsules);
        CapsuleRadii.Reserve(NumCapsules);
        CapsuleInside.Reserve(NumCapsules);
        
        PlanePoints.Reserve(NumPlanes);
        PlaneNormals.Reserve(NumPlanes);
    }
};
```

## Validation
1. Profile memory allocations before and after optimization
2. Measure frame time improvements with complex spring bone setups
3. Verify no visual differences in animation
4. Test with various numbers of colliders (0, few, many)

## Success Criteria
- Reduced per-frame allocations (target: 50% reduction)
- Improved cache hit rate for collision calculations
- No regression in animation quality
- Measurable performance improvement (target: 10-20% faster collision phase)