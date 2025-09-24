#pragma once

#include "CoreMinimal.h"
// Use the generic Interchange pipeline base (available across versions)
#include "InterchangePipelineBase.h"
#include "VRMSpringBonesPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class UVRMSpringBoneData;
class USkeleton;
class USkeletalMesh;
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

    // Phase 3 controls
    UPROPERTY(EditAnywhere, Category = "VRM Spring", meta=(DisplayName="Generate Post-Process AnimBP", ToolTip="Duplicate a template Post-Process AnimBlueprint next to the imported mesh."))
    bool bGeneratePostProcessAnimBP = false;

    UPROPERTY(EditAnywhere, Category = "VRM Spring", meta=(DisplayName="Assign Post-Process AnimBP", ToolTip="Assign the duplicated Post-Process AnimBP to the imported SkeletalMesh."))
    bool bAssignPostProcessABP = false;

    UPROPERTY(EditAnywhere, Category = "VRM Spring", meta=(DisplayName="Animation Sub-Folder"))
    FString AnimationSubFolder = TEXT("Animation");

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

    // Phase 3 helpers
    bool FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const;
    UObject* DuplicateTemplateAnimBlueprint(const FString& TargetPackagePath, const FString& BaseName, USkeleton* TargetSkeleton) const;
    bool SetSpringConfigOnAnimBlueprint(UObject* AnimBlueprintObj, UVRMSpringBoneData* SpringData) const;
    bool AssignPostProcessABPToMesh(USkeletalMesh* SkelMesh, UObject* AnimBlueprintObj) const;
#endif
};

