# Task: Implement Proper Node Index to Bone Name Mapping

## Objective
Implement proper glTF node index to Unreal bone name mapping throughout the VRM SpringBone system to ensure correct bone references.

## Current Issue
The plugin currently uses bone names directly without maintaining proper mapping from glTF node indices, which can cause incorrect bone references when VRM files use node indices that don't directly correspond to bone names.

## Required Changes

### 1. Populate NodeToBoneMap during VRM Import

**File**: `Plugins/VRMInterchange/Source/VRMInterchange/Private/VRMTranslator.cpp`

Add node mapping population in the `LoadVRM` method after parsing bones:

```cpp
// After PopulateBonesFromSkin call (around line 850)
// Build node index to bone name map for spring bone resolution
TMap<int32, FName> NodeToBoneMap;
if (Skin && Data->nodes_count > 0) {
    for (size_t nodeIdx = 0; nodeIdx < Data->nodes_count; ++nodeIdx) {
        const cgltf_node* Node = &Data->nodes[nodeIdx];
        if (Node && Node->name) {
            FString NodeName = UTF8_TO_TCHAR(Node->name);
            // Check if this node is a joint in the skeleton
            for (size_t jointIdx = 0; jointIdx < Skin->joints_count; ++jointIdx) {
                if (Skin->joints[jointIdx] == Node) {
                    NodeToBoneMap.Add(static_cast<int32>(nodeIdx), FName(*NodeName));
                    break;
                }
            }
        }
    }
}
// Store for later use in spring bone data asset creation
Parsed.NodeToBoneMap = NodeToBoneMap;
```

**File**: `Plugins/VRMInterchange/Source/VRMInterchange/Public/VRMTranslator.h`

Add to `FVRMParsedModel` struct:
```cpp
struct FVRMParsedModel
{
    // ... existing members ...
    TMap<int32, FName> NodeToBoneMap;  // Add this line
};
```

### 2. Pass Mapping to Spring Bone Data Asset

**File**: `Plugins/VRMInterchange/Source/VRMInterchangeEditor/Private/VRMSpringBonesPostImportPipeline.cpp`

Modify `ParseAndFillDataAssetFromFile` method to also populate the node map:

```cpp
bool UVRMSpringBonesPostImportPipeline::ParseAndFillDataAssetFromFile(
    const FString& Filename, 
    UVRMSpringBoneData* Dest) const
{
    if (!Dest) return false;
    FVRMSpringConfig Config; 
    FString Err;
    TMap<int32, FName> NodeMap;  // Add this
    
    if (!VRM::ParseSpringBonesFromFile(Filename, Config, NodeMap, Err))  // Modified
    {
        return false;
    }
    
    Dest->SpringConfig = MoveTemp(Config);
    Dest->SetNodeToBoneMapping(NodeMap);  // Add this
    return Dest->SpringConfig.IsValid();
}
```

### 3. Update Bone Resolution in AnimNode

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`

Replace the `ResolveBoneByNodeIndex` method:

```cpp
FCompactPoseBoneIndex FAnimNode_VRMSpringBone::ResolveBoneByNodeIndex(
    const FBoneContainer& BoneContainer, 
    int32 NodeIndex) const
{
    if (SpringConfig == nullptr || NodeIndex == INDEX_NONE) {
        return FCompactPoseBoneIndex(INDEX_NONE);
    }
    
    // First try the node to bone map
    const FName BoneName = SpringConfig->GetBoneNameForNode(NodeIndex);
    if (!BoneName.IsNone()) {
        return ResolveBone(BoneContainer, BoneName);
    }
    
    // Fallback: try to interpret node index as direct bone index (for compatibility)
    const FReferenceSkeleton& RefSkeleton = BoneContainer.GetReferenceSkeleton();
    if (NodeIndex >= 0 && NodeIndex < RefSkeleton.GetNum()) {
        const FName FallbackName = RefSkeleton.GetBoneName(NodeIndex);
        UE_LOG(LogVRMSpring, Warning, 
            TEXT("[VRMSpring] Using fallback bone resolution for node %d -> %s"), 
            NodeIndex, *FallbackName.ToString());
        return ResolveBone(BoneContainer, FallbackName);
    }
    
    return FCompactPoseBoneIndex(INDEX_NONE);
}
```

### 4. Update Cache Rebuilding

**File**: `Plugins/VRMInterchange/Source/VRMSpringBonesRuntime/Private/AnimNode_VRMSpringBone.cpp`

In `RebuildCaches_AnyThread`, update all node index resolution to use the new method:

```cpp
// Replace all instances of direct node index usage with:
FCompactPoseBoneIndex BoneIndex = ResolveBoneByNodeIndex(BoneContainer, Collider.NodeIndex);

// For joints:
FCompactPoseBoneIndex JointBone = ResolveBoneByNodeIndex(BoneContainer, Joint.NodeIndex);

// For center nodes:
Chain.CenterBoneIndex = ResolveBoneByNodeIndex(BoneContainer, Spring.CenterNodeIndex);
```

## Validation
1. Test with VRM files that use node indices instead of bone names
2. Verify correct bone mapping with complex skeletons
3. Check backwards compatibility with existing assets
4. Validate that warning logs appear for fallback resolution

## Success Criteria
- Node indices correctly resolve to bone names
- No broken bone references in imported VRM models
- Existing assets continue to work without modification
- Clear logging when fallback resolution is used