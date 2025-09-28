# Task: Add Duplicate Joint Validation for Spring Chains

## Objective
Implement validation to detect and warn when the same joint is used in multiple spring chains, which is prohibited by the VRM specification.

## Current Issue
The current implementation doesn't validate that joints are unique across spring chains, which can lead to undefined behavior and inconsistencies with the UniVRM reference implementation.

## Required Changes

### 1. Add Validation Method to AnimNode

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`

Add a new private method for validation:

```cpp
// Add to private section of FAnimNode_VRMSpringBone class header
bool ValidateNoDuplicateJoints(const FBoneContainer& BoneContainer);

// Implementation in .cpp file
bool FAnimNode_VRMSpringBone::ValidateNoDuplicateJoints(const FBoneContainer& BoneContainer)
{
    if (!SpringConfig || !SpringConfig->SpringConfig.IsValid()) {
        return true; // Nothing to validate
    }
    
    bool bIsValid = true;
    const FVRMSpringConfig& Config = SpringConfig->SpringConfig;
    
    // Map to track joint usage: JointIndex -> (SpringIndex, SpringName)
    TMap<int32, TPair<int32, FString>> JointUsage;
    
    for (int32 SpringIdx = 0; SpringIdx < Config.Springs.Num(); ++SpringIdx)
    {
        const FVRMSpring& Spring = Config.Springs[SpringIdx];
        
        for (int32 JointIdx : Spring.JointIndices)
        {
            if (JointIdx < 0 || JointIdx >= Config.Joints.Num()) {
                continue; // Skip invalid indices
            }
            
            if (TPair<int32, FString>* ExistingUsage = JointUsage.Find(JointIdx))
            {
                // Joint is already used by another spring
                const FVRMSpringJoint& Joint = Config.Joints[JointIdx];
                FString JointIdentifier = Joint.BoneName.IsNone() 
                    ? FString::Printf(TEXT("Node_%d"), Joint.NodeIndex)
                    : Joint.BoneName.ToString();
                
                UE_LOG(LogVRMSpring, Warning, 
                    TEXT("[VRMSpring] SPEC VIOLATION: Joint '%s' (index %d) is used by multiple springs: '%s' and '%s'. "
                         "This is prohibited by VRM specification and may cause undefined behavior."),
                    *JointIdentifier, JointIdx,
                    *ExistingUsage->Value, *Spring.Name);
                
                bIsValid = false;
                
                // Also check for implicit duplicates through node skipping
                // If joints are not direct parent-child, intermediate nodes are implicitly included
                if (Spring.JointIndices.Num() > 1)
                {
                    for (int32 i = 0; i < Spring.JointIndices.Num() - 1; ++i)
                    {
                        CheckImplicitDuplicates(
                            Spring.JointIndices[i], 
                            Spring.JointIndices[i + 1],
                            SpringIdx,
                            Spring.Name,
                            JointUsage,
                            BoneContainer);
                    }
                }
            }
            else
            {
                // Record this joint's usage
                JointUsage.Add(JointIdx, TPair<int32, FString>(SpringIdx, Spring.Name));
            }
        }
    }
    
    if (!bIsValid)
    {
        UE_LOG(LogVRMSpring, Error, 
            TEXT("[VRMSpring] Spring configuration contains duplicate joints across chains. "
                 "This violates VRM specification and may produce incorrect results."));
    }
    
    return bIsValid;
}

// Helper method to check for implicitly included joints
void FAnimNode_VRMSpringBone::CheckImplicitDuplicates(
    int32 ParentJointIdx,
    int32 ChildJointIdx, 
    int32 SpringIdx,
    const FString& SpringName,
    TMap<int32, TPair<int32, FString>>& JointUsage,
    const FBoneContainer& BoneContainer)
{
    // Get bone indices for parent and child
    const FVRMSpringJoint& ParentJoint = SpringConfig->SpringConfig.Joints[ParentJointIdx];
    const FVRMSpringJoint& ChildJoint = SpringConfig->SpringConfig.Joints[ChildJointIdx];
    
    FCompactPoseBoneIndex ParentBone = ResolveBone(BoneContainer, ParentJoint.BoneName);
    FCompactPoseBoneIndex ChildBone = ResolveBone(BoneContainer, ChildJoint.BoneName);
    
    if (!ParentBone.IsValid() || !ChildBone.IsValid()) {
        return;
    }
    
    // Walk up from child to parent, checking intermediate bones
    FCompactPoseBoneIndex Current = BoneContainer.GetParentBoneIndex(ChildBone);
    while (Current.IsValid() && Current != ParentBone)
    {
        // Check if this intermediate bone is explicitly used elsewhere
        FName IntermediateBoneName = BoneContainer.GetReferenceSkeleton().GetBoneName(
            BoneContainer.GetSkeletonIndex(Current));
        
        // Search for this bone in other springs
        for (int32 OtherJointIdx = 0; OtherJointIdx < SpringConfig->SpringConfig.Joints.Num(); ++OtherJointIdx)
        {
            const FVRMSpringJoint& OtherJoint = SpringConfig->SpringConfig.Joints[OtherJointIdx];
            if (OtherJoint.BoneName == IntermediateBoneName)
            {
                if (TPair<int32, FString>* ExistingUsage = JointUsage.Find(OtherJointIdx))
                {
                    if (ExistingUsage->Key != SpringIdx) // Different spring
                    {
                        UE_LOG(LogVRMSpring, Warning,
                            TEXT("[VRMSpring] Implicit duplicate: Bone '%s' is implicitly included "
                                 "in spring '%s' (between joints) but explicitly used in spring '%s'"),
                            *IntermediateBoneName.ToString(), *SpringName, *ExistingUsage->Value);
                    }
                }
            }
        }
        
        Current = BoneContainer.GetParentBoneIndex(Current);
    }
}
```

### 2. Call Validation During Cache Rebuild

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`

In `RebuildCaches_AnyThread`, add validation call after building caches:

```cpp
void FAnimNode_VRMSpringBone::RebuildCaches_AnyThread(const FBoneContainer& BoneContainer)
{
    // ... existing cache building code ...
    
    // Add validation at the end of the method
    if (bCachesValid)
    {
        bool bValidConfig = ValidateNoDuplicateJoints(BoneContainer);
        
        #if !UE_BUILD_SHIPPING
        if (!bValidConfig)
        {
            UE_LOG(LogVRMSpring, Warning, 
                TEXT("[VRMSpring] Spring configuration has validation errors. "
                     "See log for details. Animation may not match VRM specification."));
        }
        else
        {
            UE_LOG(LogVRMSpring, Log, 
                TEXT("[VRMSpring] Spring configuration validated successfully."));
        }
        #endif
    }
}
```

### 3. Add Editor-Time Validation

**File**: `Plugins/VRMInterchange/Source/VRMInterchangeEditor/Private/VRMSpringBonesPostImportPipeline.cpp`

Add validation during import:

```cpp
// In ExecutePipeline method, after parsing spring data
if (SpringDataAsset && SpringDataAsset->SpringConfig.IsValid())
{
    // Validate for duplicate joints
    TSet<int32> UsedJoints;
    TArray<FString> DuplicateWarnings;
    
    for (const FVRMSpring& Spring : SpringDataAsset->SpringConfig.Springs)
    {
        for (int32 JointIdx : Spring.JointIndices)
        {
            if (UsedJoints.Contains(JointIdx))
            {
                const FVRMSpringJoint& Joint = SpringDataAsset->SpringConfig.Joints.IsValidIndex(JointIdx) 
                    ? SpringDataAsset->SpringConfig.Joints[JointIdx] 
                    : FVRMSpringJoint();
                    
                FString JointName = Joint.BoneName.IsNone() 
                    ? FString::Printf(TEXT("Node_%d"), Joint.NodeIndex)
                    : Joint.BoneName.ToString();
                    
                DuplicateWarnings.Add(FString::Printf(
                    TEXT("Joint '%s' (index %d) used in multiple springs"),
                    *JointName, JointIdx));
            }
            UsedJoints.Add(JointIdx);
        }
    }
    
    if (DuplicateWarnings.Num() > 0)
    {
        FString WarningMessage = TEXT("VRM Spring Bone configuration has duplicate joints:\n");
        for (const FString& Warning : DuplicateWarnings)
        {
            WarningMessage += TEXT("  - ") + Warning + TEXT("\n");
        }
        
        UE_LOG(LogVRMSpring, Warning, TEXT("%s"), *WarningMessage);
        
        // Optionally show editor notification
        #if WITH_EDITOR
        FVRMInterchangeEditorModule::ShowNotification(
            FText::FromString(TEXT("VRM Spring configuration has validation warnings. Check log for details.")),
            SNotificationItem::CS_Fail);
        #endif
    }
}
```

## Validation
1. Test with VRM models that have duplicate joints across springs
2. Verify warnings appear in the log
3. Test with valid configurations to ensure no false positives
4. Check that branching chains are properly handled

## Success Criteria
- Duplicate joints are detected and logged with clear warnings
- Validation doesn't break existing valid configurations
- Performance impact is minimal
- Clear actionable warnings help users fix their configurations