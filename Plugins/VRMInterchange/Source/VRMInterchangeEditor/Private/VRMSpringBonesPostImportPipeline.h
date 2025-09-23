#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "VRMSpringBonesPostImportPipeline.generated.h"

// Forward declarations to avoid including heavy headers here
class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class UVRMSpringBoneData;
class USkeleton;
struct FVRMSpringConfig;

UCLASS()
class UVRMSpringBonesPostImportPipeline : public UInterchangePipelineBase
{
    GENERATED_BODY()
public:
#if WITH_EDITOR
    // Visible toggle in Project Settings > Interchange > Pipelines
    UPROPERTY(EditAnywhere, Category = "VRM Spring")
    bool bGenerateSpringBoneData = false;

    // Create or overwrite existing asset with same name
    UPROPERTY(EditAnywhere, Category = "VRM Spring")
    bool bOverwriteExisting = false;

    // Folder suffix under the root imported package
    UPROPERTY(EditAnywhere, Category = "VRM Spring")
    FString SubFolder = TEXT("SpringBones");

    // Base name of the data asset
    UPROPERTY(EditAnywhere, Category = "VRM Spring")
    FString DataAssetName = TEXT("SpringBonesData");
#endif

    // UInterchangePipelineBase
    virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

private:
#if WITH_EDITOR
    // Parse and fill directly from source file (delegates JSON extraction to shared parser)
    bool ParseAndFillDataAssetFromFile(const FString& Filename, UVRMSpringBoneData* Dest) const;

    // Use ContentBasePath when available; fallback to /Game/<VRMBaseName>
    FString MakeTargetPathAndName(const FString& SourceFilename, const FString& ContentBasePath, FString& OutPackagePath, FString& OutAssetName) const;

    // Resolve BoneName from glTF node indices using cgltf (editor-only)
    bool ResolveBoneNamesFromFile(const FString& Filename, FVRMSpringConfig& InOut, int32& OutResolvedColliders, int32& OutResolvedJoints, int32& OutResolvedCenters) const;

    // Validate resolved BoneNames against a USkeleton found under the imported asset's folder
    void ValidateBoneNamesAgainstSkeleton(const FString& SearchRootPackagePath, const FVRMSpringConfig& Config) const;
#endif
};

