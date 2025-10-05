#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "VRMSpringBoneData.h" // Ensure UVRMSpringBoneData is a complete type here
#include "VRMSpringBonesPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class USkeleton;
class USkeletalMesh;
class UFactory;
struct FVRMSpringConfig;

/**
 * VRM Spring Bones (Post-Import)
 *
 * - Parses VRM spring bone data and materializes a SpringBone DataAsset after import confirmation.
 * - Optionally duplicates a Post-Process AnimBP, injects the SpringConfig, and assigns it to the SkeletalMesh.
 * - Does NOT save packages during import; marks packages dirty so Save All/SCC handle persistence.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM Spring Bones (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMSpringBonesPostImportPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	UVRMSpringBonesPostImportPipeline() = default;

#if WITH_EDITOR
	virtual void PostInitProperties() override;

	/** The name of the pipeline that will be display in the import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName = "VRM Spring Bones Import and Configuration";	// Dialog toggles (defaults loaded from project settings)
  
	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bGenerateSpringBoneData = true;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bOverwriteExisting = false;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bGeneratePostProcessAnimBP = false;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bAssignPostProcessABP = false;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bOverwriteExistingPostProcessABP = false;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bReusePostProcessABPOnReimport = true;

	// Convert VRM units (meters) to UE units (centimeters) for SpringData on import.
	// Disable if your source already uses UE scale.
	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	bool bConvertToUEUnits = true;

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	FString AnimationSubFolder = TEXT("SpringBones");

	UPROPERTY(EditAnywhere, Category = "VRM Spring")
	FString SubFolder = TEXT("SpringBones");
#endif

	// UInterchangePipelineBase
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

#if WITH_EDITOR
	virtual void BeginDestroy() override;
#endif

private:
#if WITH_EDITOR
	// Parsing/materialization helpers
	bool ParseAndFillDataAssetFromFile(const FString& Filename, UVRMSpringBoneData* Dest) const;
	FString MakeTargetPathAndName(const FString& SourceFilename, const FString& ContentBasePath, FString& OutPackagePath, FString& OutAssetName) const;
	bool ResolveBoneNamesFromFile(const FString& Filename, FVRMSpringConfig& InOut, int32& OutResolvedColliders, int32& OutResolvedJoints, int32& OutResolvedCenters) const;
	void ValidateBoneNamesAgainstSkeleton(const FString& SearchRootPackagePath, const FVRMSpringConfig& Config) const;
	void ConvertSpringConfigToUEUnits(FVRMSpringConfig& InOut) const;

	// Asset helpers
	bool FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const;
	UObject* DuplicateTemplateAnimBlueprint(const FString& TargetPackagePath, const FString& BaseName, USkeleton* TargetSkeleton, bool bOverwriteExistingABP) const;
	bool SetSpringConfigOnAnimBlueprint(UObject* AnimBlueprintObj, UVRMSpringBoneData* SpringData) const;
	bool AssignPostProcessABPToMesh(USkeletalMesh* SkelMesh, UObject* AnimBlueprintObj) const;
	FString GetParentPackagePath(const FString& InPath) const;

	// Post-import deferral
	void RegisterPostImportCommit();
	void UnregisterPostImportCommit();
	void OnAssetPostImport(class UFactory* InFactory, UObject* InCreatedObject);

	// Deferred state for post-import commit
	FDelegateHandle ImportPostHandle;
	FString DeferredSkeletonSearchRoot;
	FString DeferredAltSkeletonSearchRoot; // parent of root
	FString DeferredPackagePath;
	TStrongObjectPtr<UVRMSpringBoneData> DeferredSpringDataTransient;
	bool bDeferredWantsAssign = false;
	bool bDeferredCompleted = false;
	FString DeferredAnimFolder;
	FString DeferredDesiredABPName;
	bool bDeferredOverwriteABP = false;
	bool bDeferredOverwriteSpringAsset = false;
	bool bDeferredReuseABP = true;
	FString DeferredSourceFilename;
	FString DeferredSourceHash;
#endif
};

