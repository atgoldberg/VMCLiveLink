#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VRMInterchangeSettings.generated.h"

UCLASS(config=Game, defaultconfig, meta=(DisplayName="VRM Interchange"))
class VRMINTERCHANGEEDITOR_API UVRMInterchangeSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
	UVRMInterchangeSettings();

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Parse and generate spring bone data assets during import."))
    bool bGenerateSpringBoneData = true;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Duplicate and assign a Post-Process AnimBP to drive springs."))
    bool bGeneratePostProcessAnimBP = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Assign generated Post-Process AnimBP to the imported SkeletalMesh."))
    bool bAssignPostProcessABP = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, overwrite existing generated spring assets. If false, create with a suffix."))
    bool bOverwriteExistingSpringAssets = false;

    // New: separate control for overwriting generated Post-Process AnimBlueprints
    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, overwrite existing generated Post-Process AnimBlueprints. If false, create with a unique name."))
    bool bOverwriteExistingPostProcessABP = false;

    // New: control whether to reuse an existing Post-Process ABP on re-import (preferred safe default)
    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, attempt to reuse an existing Post-Process AnimBP when re-importing. If false, the importer will offer to overwrite or create a new ABP."))
    bool bReusePostProcessABPOnReimport = true;

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};