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
    UPROPERTY(EditAnywhere, Category = "VRM Spring")
    bool bGenerateSpringBoneData = false;

    UPROPERTY(EditAnywhere, Category = "VRM Spring")
    bool bOverwriteExisting = false;

    UPROPERTY(EditAnywhere, Category = "VRM Spring")
    FString SubFolder = TEXT("SpringBones");

    UPROPERTY(EditAnywhere, Category = "VRM Spring")
    FString DataAssetName = TEXT("SpringBonesData");
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

