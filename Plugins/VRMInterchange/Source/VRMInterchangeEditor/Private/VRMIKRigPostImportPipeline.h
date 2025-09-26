#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "VRMIKRigPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class USkeletalMesh;
class USkeleton;
class UIKRigDefinition;

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM IK Rig (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMIKRigPostImportPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	UVRMIKRigPostImportPipeline() = default;

#if WITH_EDITOR
	// Whether to generate an IK Rig asset next to the imported mesh
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	bool bGenerateIKRig = true;

	// Overwrite existing asset with same name
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	bool bOverwriteExisting = false;

	// Subfolder under character folder to place the asset
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	FString AnimationSubFolder = TEXT("Animation");

	// Base name of the generated asset
	UPROPERTY(EditAnywhere, Category = "VRM IK Rig")
	FString AssetBaseName = TEXT("IK_Rig_VRM");
#endif

	// UInterchangePipelineBase
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

private:
#if WITH_EDITOR
	bool FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const;
	FString GetParentPackagePath(const FString& InPath) const;
	bool DuplicateTemplateIKRig(const FString& TargetPackagePath, const FString& BaseName, UIKRigDefinition*& OutIKRig, bool bOverwrite) const;
	FString MakeCharacterBasePath(const FString& SourceFilename, const FString& ContentBasePath) const;

	// Deferred creation if skeletal assets are not available yet
	void RegisterDeferredIKRig(const FString& InSkeletonSearchRoot, const FString& InPackagePath);
	void UnregisterDeferredIKRig();
	void OnAssetAddedForDeferredIKRig(const struct FAssetData& AssetData);

	// Deferred state
	FDelegateHandle DeferredHandle;
	FString DeferredSkeletonSearchRoot;
	FString DeferredAltSkeletonSearchRoot;
	FString DeferredPackagePath; // character base path
	bool bDeferredCompleted = false;
#endif
};
