# Task: Implement Parallel Processing for Independent Spring Chains

## Objective
Implement parallel processing for spring chains that have no dependencies, utilizing Unreal's ParallelFor to improve performance on multi-core systems.

## Current Issue
All spring chains are processed sequentially even when they have no interdependencies, missing optimization opportunities on modern multi-core CPUs.

## Required Changes

### 1. Add Chain Dependency Analysis

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`

Add dependency analysis during cache rebuild:

```cpp
// Add to FVRMSBChainCache structure
struct FVRMSBChainCache
{
    // ... existing members ...
    
    // NEW: Dependency tracking
    TArray<int32> DependsOnChains;  // Indices of chains this chain depends on
    TArray<int32> DependentChains;  // Indices of chains that depend on this chain
    bool bCanProcessParallel = false;
};

// Add analysis method
void FAnimNode_VRMSpringBone::AnalyzeChainDependencies()
{
    const int32 NumChains = ChainCaches.Num();
    
    // Build bone to chain mapping
    TMap<FCompactPoseBoneIndex, int32> BoneToChainMap;
    for (int32 ChainIdx = 0; ChainIdx < NumChains; ++ChainIdx)
    {
        FVRMSBChainCache& Chain = ChainCaches[ChainIdx];
        Chain.DependsOnChains.Reset();
        Chain.DependentChains.Reset();
        
        for (const FVRMSBJointCache& Joint : Chain.Joints)
        {
            if (Joint.bValid && Joint.BoneIndex.IsValid())
            {
                BoneToChainMap.Add(Joint.BoneIndex, ChainIdx);
            }
        }
    }
    
    // Analyze dependencies
    for (int32 ChainIdx = 0; ChainIdx < NumChains; ++ChainIdx)
    {
        FVRMSBChainCache& Chain = ChainCaches[ChainIdx];
        TSet<int32> Dependencies;
        
        // Check if any joint's parent belongs to another chain
        for (const FVRMSBJointCache& Joint : Chain.Joints)
        {
            if (!Joint.bValid || !Joint.BoneIndex.IsValid()) continue;
            
            // Check parent bone
            FCompactPoseBoneIndex ParentBone = BoneContainer.GetParentBoneIndex(Joint.BoneIndex);
            while (ParentBone.IsValid())
            {
                if (int32* OtherChainIdx = BoneToChainMap.Find(ParentBone))
                {
                    if (*OtherChainIdx != ChainIdx)
                    {
                        Dependencies.Add(*OtherChainIdx);
                        break; // Found a dependency, no need to check further up
                    }
                }
                ParentBone = BoneContainer.GetParentBoneIndex(ParentBone);
            }
            
            // Check if center node belongs to another chain
            if (Chain.bHasCenter && Chain.CenterBoneIndex.IsValid())
            {
                if (int32* OtherChainIdx = BoneToChainMap.Find(Chain.CenterBoneIndex))
                {
                    if (*OtherChainIdx != ChainIdx)
                    {
                        Dependencies.Add(*OtherChainIdx);
                    }
                }
            }
        }
        
        // Store dependencies
        Chain.DependsOnChains = Dependencies.Array();
        Chain.bCanProcessParallel = (Chain.DependsOnChains.Num() == 0);
        
        // Update dependent chains
        for (int32 DepIdx : Chain.DependsOnChains)
        {
            ChainCaches[DepIdx].DependentChains.Add(ChainIdx);
        }
    }
    
    UE_LOG(LogVRMSpring, Log, 
        TEXT("[VRMSpring] Chain dependency analysis: %d chains, %d can process in parallel"),
        NumChains, 
        Algo::CountIf(ChainCaches, [](const FVRMSBChainCache& C) { return C.bCanProcessParallel; }));
}

// Call in RebuildCaches_AnyThread after building chains
void FAnimNode_VRMSpringBone::RebuildCaches_AnyThread(const FBoneContainer& BoneContainer)
{
    // ... existing cache building ...
    
    // Analyze dependencies for parallel processing
    AnalyzeChainDependencies();
    
    // ... rest of method ...
}
```

### 2. Implement Parallel Chain Processing

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`

Replace SimulateChains with parallel version:

```cpp
void FAnimNode_VRMSpringBone::SimulateChains(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& CSPose)
{
    if (ChainCaches.Num() == 0) return;
    
    // Group chains by dependency level
    TArray<TArray<int32>> ProcessingGroups;
    GroupChainsByDependencyLevel(ProcessingGroups);
    
    // Process each group
    for (const TArray<int32>& Group : ProcessingGroups)
    {
        if (Group.Num() == 1)
        {
            // Single chain in group, process directly
            SimulateSingleChain(ChainCaches[Group[0]], BoneContainer, CSPose);
        }
        else if (Group.Num() > 1)
        {
            // Multiple independent chains, process in parallel
            const int32 MinBatchSize = 1; // Process each chain as separate task
            
            ParallelFor(Group.Num(), [&](int32 LocalIdx)
            {
                const int32 ChainIdx = Group[LocalIdx];
                SimulateSingleChain(ChainCaches[ChainIdx], BoneContainer, CSPose);
            }, 
            EParallelForFlags::None, 
            MinBatchSize);
        }
    }
}

// Extract single chain simulation logic
void FAnimNode_VRMSpringBone::SimulateSingleChain(
    FVRMSBChainCache& Chain, 
    const FBoneContainer& BoneContainer, 
    FCSPose<FCompactPose>& CSPose)
{
    if (Chain.Joints.Num() == 0) return;
    
    FTransform CenterCST = FTransform::Identity;
    FTransform CenterInv = FTransform::Identity;
    FQuat CenterRot = FQuat::Identity;
    FQuat CenterRotInv = FQuat::Identity;
    
    if (Chain.bHasCenter && Chain.CenterBoneIndex.IsValid())
    {
        CenterCST = CSPose.GetComponentSpaceTransform(Chain.CenterBoneIndex);
        CenterInv = CenterCST.Inverse();
        CenterRot = CenterCST.GetRotation();
        CenterRotInv = CenterRot.Inverse();
    }
    
    // Thread-local collision resolve lambdas
    auto ResolveSphere = [](const FVector& Center, float Radius, bool bInside, FVector& Tip, float TipRadius)
    {
        // ... existing sphere collision code ...
    };
    
    auto ResolveCapsule = [](const FVector& A, const FVector& B, float Radius, bool bInside, FVector& Tip, float TipRadius)
    {
        // ... existing capsule collision code ...
    };
    
    auto ResolvePlane = [](const FVector& P0, const FVector& N, FVector& Tip, float TipRadius)
    {
        // ... existing plane collision code ...
    };
    
    // Simulation substeps
    for (int32 Step = 0; Step < CachedSubsteps; ++Step)
    {
        const float h = CachedH;
        const float s60 = h * 60.f;
        const float DragClamped = FMath::Clamp(Chain.Drag, 0.f, 1.f);
        const float DampFloor = 0.01f;
        const float RetainFactor = FMath::Max(DampFloor, 1.f - DragClamped);
        const float StiffAlpha = FMath::Min(1.f, Chain.Stiffness * s60);
        
        // Process each joint in the chain
        for (int32 j = 0; j < Chain.Joints.Num(); ++j)
        {
            FVRMSBJointCache& JC = Chain.Joints[j];
            if (!JC.bValid || JC.RestLength <= KINDA_SMALL_NUMBER) continue;
            
            // ... existing joint simulation code ...
            // (keep all the physics calculations as-is)
        }
    }
}

// Group chains by dependency level for parallel processing
void FAnimNode_VRMSpringBone::GroupChainsByDependencyLevel(TArray<TArray<int32>>& OutGroups)
{
    OutGroups.Reset();
    const int32 NumChains = ChainCaches.Num();
    if (NumChains == 0) return;
    
    TArray<int32> ChainLevel;
    ChainLevel.SetNum(NumChains);
    
    // Calculate dependency level for each chain
    int32 MaxLevel = 0;
    for (int32 ChainIdx = 0; ChainIdx < NumChains; ++ChainIdx)
    {
        ChainLevel[ChainIdx] = CalculateDependencyLevel(ChainIdx);
        MaxLevel = FMath::Max(MaxLevel, ChainLevel[ChainIdx]);
    }
    
    // Group chains by level
    OutGroups.SetNum(MaxLevel + 1);
    for (int32 ChainIdx = 0; ChainIdx < NumChains; ++ChainIdx)
    {
        OutGroups[ChainLevel[ChainIdx]].Add(ChainIdx);
    }
    
    // Log grouping info
    #if !UE_BUILD_SHIPPING
    for (int32 Level = 0; Level < OutGroups.Num(); ++Level)
    {
        if (OutGroups[Level].Num() > 1)
        {
            UE_LOG(LogVRMSpring, Verbose, 
                TEXT("[VRMSpring] Parallel group %d: %d chains"), 
                Level, OutGroups[Level].Num());
        }
    }
    #endif
}

// Recursively calculate dependency level
int32 FAnimNode_VRMSpringBone::CalculateDependencyLevel(int32 ChainIdx)
{
    const FVRMSBChainCache& Chain = ChainCaches[ChainIdx];
    if (Chain.DependsOnChains.Num() == 0)
    {
        return 0; // No dependencies, can process first
    }
    
    int32 MaxDepLevel = 0;
    for (int32 DepIdx : Chain.DependsOnChains)
    {
        MaxDepLevel = FMath::Max(MaxDepLevel, CalculateDependencyLevel(DepIdx));
    }
    return MaxDepLevel + 1;
}
```

### 3. Add Threading Configuration

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Public/AnimNode_VRMSpringBone.h`

Add configuration options:

```cpp
public:
    // ... existing members ...
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Performance", 
        meta=(DisplayName="Enable Parallel Processing", 
        ToolTip="Process independent spring chains in parallel for better multi-core performance"))
    bool bEnableParallelProcessing = true;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spring|Performance", 
        meta=(EditCondition="bEnableParallelProcessing", ClampMin="1", ClampMax="16",
        ToolTip="Minimum number of chains to justify parallel processing overhead"))
    int32 MinChainsForParallel = 2;
```

Update SimulateChains to check configuration:

```cpp
void FAnimNode_VRMSpringBone::SimulateChains(const FBoneContainer& BoneContainer, FCSPose<FCompactPose>& CSPose)
{
    // Check if parallel processing is beneficial
    const bool bUseParallel = bEnableParallelProcessing 
        && ChainCaches.Num() >= MinChainsForParallel
        && FPlatformMisc::NumberOfCores() > 1;
    
    if (!bUseParallel)
    {
        // Fall back to sequential processing
        for (FVRMSBChainCache& Chain : ChainCaches)
        {
            SimulateSingleChain(Chain, BoneContainer, CSPose);
        }
        return;
    }
    
    // ... parallel processing code from above ...
}
```

## Validation
1. Profile performance with models having many independent spring chains
2. Verify no visual differences between parallel and sequential processing
3. Test thread safety with multiple characters
4. Measure CPU utilization across cores
5. Test with various chain counts (1, 2, 10, 100+)

## Success Criteria
- Performance improvement scales with core count (target: 2-4x on quad-core)
- No race conditions or thread safety issues
- Identical animation results to sequential processing
- Minimal overhead for single-chain scenarios
- CPU utilization shows proper multi-core usage