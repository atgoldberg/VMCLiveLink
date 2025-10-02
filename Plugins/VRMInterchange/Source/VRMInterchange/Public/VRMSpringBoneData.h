#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VRMSpringBonesTypes.h"
#include "VRMSpringBoneData.generated.h"

// Runtime-available data asset storing parsed spring bone configuration.
// Moved from Editor module to allow packaged builds to access spring settings.
UCLASS(BlueprintType)
class VRMINTERCHANGE_API UVRMSpringBoneData : public UDataAsset
{
    GENERATED_BODY()
public:
    // Show inner struct fields directly so users can inspect arrays like Colliders/Joints/Springs
    UPROPERTY(EditAnywhere, Category="Spring Bones", meta=(ShowOnlyInnerProperties))
    FVRMSpringConfig SpringConfig;

        // Full glTF node hierarchy (per avatar), persisted with the asset.
    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TMap<int32, int32> NodeParent;               // NodeIndex -> ParentNodeIndex (INDEX_NONE if root)

    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TMap<int32, FVRMNodeChildren> NodeChildren;     // NodeIndex -> Children NodeIndices

    // For each joint in SpringConfig.Joints, which *actual* child node forms its tail.
    // INDEX_NONE means terminal joint (use VRM0 pseudo-tail fallback).
    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TArray<int32> ResolvedChildNodeIndexPerJoint;

    // IMPROVED: Add node index to bone name mapping for proper VRM compliance
    // This should be populated during VRM import to maintain proper mapping from glTF node indices to UE bone names
    UPROPERTY(VisibleAnywhere, Category="Spring Bones", meta=(ToolTip="Mapping from VRM/glTF node indices to Unreal bone names"))
    TMap<int32, FName> NodeToBoneMap;

    // Original source hash derived from imported file contents
    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceHash;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceFilename;

    // Bumped when a user edits high-level tunables so the anim node rebuilds (appended to SourceHash logic)
    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    int32 EditRevision = 0;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;

// Compute ResolvedChildNodeIndexPerJoint after SpringConfig is filled + NodeChildren set
    void BuildResolvedChildren();

#endif

    // Utility to get a combined dynamic hash used by runtime to detect changes
    FString GetEffectiveHash() const { return SourceHash + TEXT("_") + FString::FromInt(EditRevision); }
    
    // IMPROVED: Helper function to resolve bone name from VRM node index
    FName GetBoneNameForNode(int32 NodeIndex) const
    {
        if (const FName* BoneName = NodeToBoneMap.Find(NodeIndex))
        {
            return *BoneName;
        }
        return NAME_None;
    }
    
    // Helper to set node to bone mapping (should be called during VRM import)
    void SetNodeToBoneMapping(const TMap<int32, FName>& InNodeToBoneMap)
    {
        NodeToBoneMap = InNodeToBoneMap;
    }

};
