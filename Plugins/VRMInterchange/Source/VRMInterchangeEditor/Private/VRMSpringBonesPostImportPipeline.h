#pragma once

#include "CoreMinimal.h"
// Use the generic Interchange pipeline base (available across versions)
#include "InterchangePipelineBase.h"
#include "VRMSpringBonesPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class UVRMSpringBoneData;
class USkeleton;
struct FVRMSpringConfig;

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM Spring Bones (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMSpringBonesPostImportPipeline : public UInterchangePipelineBase
{
    GENERATED_BODY()
public:
    UVRMSpringBonesPostImportPipeline() = default;

#if WITH_EDITOR
    // Exposed to the import dialog; default value will be initialized from project settings
    UPROPERTY(EditAnywhere, Category = "VRM Spring", meta=(DisplayName="Generate Spring Bone Data", ToolTip="Parse VRM spring bone extensions and create a SpringBones DataAsset next to the imported mesh."))
    bool bGenerateSpringBoneData = true;

    UPROPERTY(EditAnywhere, Category = "VRM Spring", meta=(DisplayName="Overwrite Existing", ToolTip="Overwrite existing generated assets. If disabled, a unique name will be chosen."))
    bool bOverwriteExisting = false;

    UPROPERTY(EditAnywhere, Category = "VRM Spring", meta=(DisplayName="Sub-Folder"))
    FString SubFolder = TEXT("SpringBones");

    UPROPERTY(EditAnywhere, Category = "VRM Spring", meta=(DisplayName="Data Asset Name"))
    FString DataAssetName = TEXT("SpringBonesData");

    // Initialize per-instance defaults from project settings so the import dialog reflects user preferences
    virtual void PostInitProperties() override;
#endif

    // UInterchangePipelineBase
    virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

private:
#if WITH_EDITOR
    bool ParseAndFillDataAssetFromFile(const FString& Filename, UVRMSpringBoneData* Dest) const;
    FString MakeTargetPathAndName(const FString& SourceFilename, const FString& ContentBasePath, FString& OutPackagePath, FString& OutAssetName) const;
    bool ResolveBoneNamesFromFile(const FString& Filename, FVRMSpringConfig& InOut, int32& OutResolvedColliders, int32& OutResolvedJoints, int32& OutResolvedCenters) const;
    void ValidateBoneNamesAgainstSkeleton(const FString& SearchRootPackagePath, const FVRMSpringConfig& Config) const;
#endif
};

