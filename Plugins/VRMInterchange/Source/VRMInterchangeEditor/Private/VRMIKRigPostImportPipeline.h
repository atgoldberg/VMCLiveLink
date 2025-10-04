#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "VRMIKRigPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class USkeletalMesh;
class USkeleton;
class UIKRigDefinition;
class UFactory;

/**
 * VRM IK Rig (Post-Import)
 *
 * - Runs after the user confirms the Interchange import dialog.
 * - Duplicates an IK Rig asset from a template, targets the imported Skeleton/SkeletalMesh.
 * - Sets the preview mesh on the IK Rig when possible.
 * - Does NOT save packages during import; marks packages dirty so Save All/SCC handle persistence.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM IK Rig (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMIKRigPostImportPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	UVRMIKRigPostImportPipeline() = default;

#if WITH_EDITOR
	/** Generate IK Rig asset next to the imported mesh */
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	bool bGenerateIKRig = true;

	/** If true, overwrite existing asset with same name; otherwise create a unique name */
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	bool bOverwriteExisting = false;

	/** Subfolder under character folder to place the asset */
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	FString AnimationSubFolder = TEXT("Animation");

	/** Base name prefix for the IK Rig (actual name includes the character suffix) */
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	FString AssetBaseName = TEXT("IK_Rig_VRM");
#endif

	// UInterchangePipelineBase
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

#if WITH_EDITOR
	virtual void BeginDestroy() override;
#endif

private:
#if WITH_EDITOR
	bool FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const;
	FString GetParentPackagePath(const FString& InPath) const;
	bool DuplicateTemplateIKRig(const FString& TargetPackagePath, const FString& BaseName, UIKRigDefinition*& OutIKRig, bool bOverwrite) const;
	FString MakeCharacterBasePath(const FString& SourceFilename, const FString& ContentBasePath) const;

	// Post-import deferral
	void RegisterPostImportCommit();
	void UnregisterPostImportCommit();
	void OnAssetPostImport(class UFactory* InFactory, UObject* InCreatedObject);

	// Deferred state for post-import commit
	FDelegateHandle ImportPostHandle;
	FString DeferredSkeletonSearchRoot;
	FString DeferredAltSkeletonSearchRoot;
	FString DeferredPackagePath;
	bool bDeferredCompleted = false;

	// Naming/location
	FString DeferredAnimFolder;
	FString DeferredDesiredIKName;
	bool bDeferredOverwriteIK = false;
#endif
};
