#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VRMSpringBonesTypes.h"
#include "VRMSpringBoneData.generated.h"

// Runtime-available data asset storing parsed spring bone configuration.
UCLASS(BlueprintType)
class VRMSPRINGBONESRUNTIME_API UVRMSpringBoneData : public UDataAsset
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere, Category="Spring Bones", meta=(ShowOnlyInnerProperties))
    FVRMSpringConfig SpringConfig;

    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TMap<int32, int32> NodeParent;

    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TMap<int32, FVRMNodeChildren> NodeChildren;

    UPROPERTY(VisibleAnywhere, Category="VRM|Hierarchy")
    TArray<int32> ResolvedChildNodeIndexPerJoint;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones", meta=(ToolTip="Mapping from VRM/glTF node indices to Unreal bone names"))
    TMap<int32, FName> NodeToBoneMap;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceHash;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceFilename;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    int32 EditRevision = 0;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    void BuildResolvedChildren();
#endif

    FString GetEffectiveHash() const { return SourceHash + TEXT("_") + FString::FromInt(EditRevision); }

    FName GetBoneNameForNode(int32 NodeIndex) const
    {
        if (const FName* BoneName = NodeToBoneMap.Find(NodeIndex))
        {
            return *BoneName;
        }
        return NAME_None;
    }

    void SetNodeToBoneMapping(const TMap<int32, FName>& InNodeToBoneMap)
    {
        NodeToBoneMap = InNodeToBoneMap;
    }
};