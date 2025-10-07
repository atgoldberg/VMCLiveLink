// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "InterchangePipelineBase.h"
#include "VRMLiveLinkPostImportPipeline.generated.h"

class UInterchangeBaseNodeContainer;
class UInterchangeSourceData;
class USkeletalMesh;
class USkeleton;
class UBlueprint;
class UAnimBlueprint;
class UFactory;

/**
 * VRM Live Link (Post-Import)
 *
 * - Runs after the user confirms the Interchange import dialog.
 * - Duplicates a character Actor Blueprint and an AnimBlueprint from templates.
 * - Wires up the imported SkeletalMesh to the actor and sets the preview mesh on the AnimBP.
 * - Does NOT save packages during import; marks packages dirty so Save All/SCC handle persistence.
 */
UCLASS(BlueprintType, EditInlineNew, DefaultToInstanced, ClassGroup=(Interchange), meta=(DisplayName="VRM Live Link (Post-Import)"))
class VRMINTERCHANGEEDITOR_API UVRMLiveLinkPostImportPipeline : public UInterchangePipelineBase
{
	GENERATED_BODY()
public:
	UVRMLiveLinkPostImportPipeline() = default;

#if WITH_EDITOR
	/** The name of the pipeline that will be display in the import dialog. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Common", meta = (StandAlonePipelineProperty = "True", PipelineInternalEditionData = "True"))
	FString PipelineDisplayName = "Live Link Actor Set-up";

	/** Generate the LiveLink-enabled Actor + AnimBP scaffold assets */
	UPROPERTY(EditAnywhere, Category = "VRM Character")
	bool bGenerateLiveLinkEnabledActor = true;
	/** Generate the LiveLink-enabled Actor + AnimBP scaffold assets */

	UPROPERTY(EditAnywhere, Category = "VRM Character")
	bool bGenerateLiveLinkRetargetActor = true;

	/** If true, overwrite existing assets with the same names; otherwise create unique names */
	UPROPERTY(EditAnywhere, Category="VRM Character")
	bool bOverwriteExisting = false;

	/** Subfolder under the character LiveLink folder to place the AnimBP */
	UPROPERTY(EditAnywhere, Category="VRM Character")
	FString AnimationSubFolder = TEXT("Animation");
#endif

	// UInterchangePipelineBase
	virtual void ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath) override;

#if WITH_EDITOR
	virtual void BeginDestroy() override;
#endif

private:
#if WITH_EDITOR
	/** Locate a SkeletalMesh and/or Skeleton under a given package root */
	bool FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const;

	/** Get the parent package path of the given path */
	FString GetParentPackagePath(const FString& InPath) const;

	/** Compute base package path for a character from the source filename/content root */
	FString MakeCharacterBasePath(const FString& SourceFilename, const FString& ContentBasePath) const;

	/** Duplicate an asset from a template path into the target package; compiles blueprints; no save */
	UObject* DuplicateTemplate(const TCHAR* TemplatePath, const FString& TargetPackagePath, const FString& DesiredName, bool bOverwrite) const;

	/** Assign a SkeletalMesh to the first SkeletalMeshComponent on the actor blueprint CDO; marks dirty */
	bool AssignSkeletalMeshToActorBP(UObject* ActorBlueprintObj, USkeletalMesh* SkeletalMesh) const;

	bool AssignSkeletalMeshToActorBPProperty(UObject* ActorBlueprintObj, USkeletalMesh* SkeletalMesh) const;

	/** Assign an AnimBP class to the actor blueprint’s SkeletalMeshComponent; marks dirty */
	bool AssignAnimBPToActorBP(UObject* ActorBlueprintObj, UAnimBlueprint* AnimBP) const;

	/** Set preview mesh on an AnimBP; marks dirty */
	bool SetPreviewMeshOnAnimBP(UAnimBlueprint* AnimBP, USkeletalMesh* SkeletalMesh) const;

	/** Post-import commit registration (UImportSubsystem) */
	void RegisterPostImportCommit();
	void UnregisterPostImportCommit();
	void OnAssetPostImport(class UFactory* InFactory, UObject* InCreatedObject);

	// Compute a robust character name using the imported mesh name if available, else the last segment of the package path
	FString ResolveEffectiveCharacterName(USkeletalMesh* SkelMesh, const FString& PackagePath) const;

	// Deferred state for post-import commit
	FDelegateHandle ImportPostHandle;
	FString DeferredSkeletonSearchRoot;
	FString DeferredAltSkeletonSearchRoot;
	FString DeferredPackagePath;
	bool bDeferredCompleted=false;

	// Names and locations staged during ExecutePipeline
	FString DeferredActorBPPath;
	FString DeferredAnimBPPath;
	FString DeferredActorBPName;
	FString DeferredAnimBPName;
	FString DeferredRetargetActorBPName;
	bool bDeferredOverwrite=false;
#endif
};
