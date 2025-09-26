#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "VRMCharacterScaffoldPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class USkeletalMesh;
class USkeleton;
class UBlueprint;
class UAnimBlueprint;

UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM Character Scaffold (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMCharacterScaffoldPostImportPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	UVRMCharacterScaffoldPostImportPipeline() = default;

#if WITH_EDITOR
	UPROPERTY(EditAnywhere, Category="VRM Scaffold")
	bool bGenerateScaffold = true;

	UPROPERTY(EditAnywhere, Category="VRM Scaffold")
	bool bOverwriteExisting = false;

	UPROPERTY(EditAnywhere, Category="VRM Scaffold")
	FString AnimationSubFolder = TEXT("Animation");
#endif

	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

private:
#if WITH_EDITOR
	bool FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const;
	FString GetParentPackagePath(const FString& InPath) const;
	FString MakeCharacterBasePath(const FString& SourceFilename, const FString& ContentBasePath) const;

	UObject* DuplicateTemplate(const TCHAR* TemplatePath, const FString& TargetPackagePath, const FString& DesiredName, bool bOverwrite) const;
	bool AssignSkeletalMeshToActorBP(UObject* ActorBlueprintObj, USkeletalMesh* SkeletalMesh) const;
	bool AssignAnimBPToActorBP(UObject* ActorBlueprintObj, UAnimBlueprint* AnimBP) const;
	bool SetPreviewMeshOnAnimBP(UAnimBlueprint* AnimBP, USkeletalMesh* SkeletalMesh) const;

	void RegisterDeferred(const FString& InSkeletonSearchRoot, const FString& InPackagePath);
	void UnregisterDeferred();
	void OnAssetAddedDeferred(const struct FAssetData& AssetData);

	// Deferred state
	FDelegateHandle DeferredHandle;
	FString DeferredSkeletonSearchRoot;
	FString DeferredAltSkeletonSearchRoot;
	FString DeferredPackagePath;
	bool bDeferredCompleted=false;
	// Cached names
	FString DeferredActorBPPath;
	FString DeferredAnimBPPath;
	FString DeferredActorBPName;
	FString DeferredAnimBPName;
	bool bDeferredOverwrite=false;
#endif
};
