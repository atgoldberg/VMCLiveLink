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
    bool bGenerateSpringBoneData = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Duplicate and assign a Post-Process AnimBP to drive springs."))
    bool bGeneratePostProcessAnimBP = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="Assign generated Post-Process AnimBP to the imported SkeletalMesh."))
    bool bAssignPostProcessABP = false;

    UPROPERTY(EditAnywhere, config, Category="Spring Bones", meta=(ToolTip="If true, overwrite existing generated spring assets. If false, create with a suffix."))
    bool bOverwriteExistingSpringAssets = false;

    virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};