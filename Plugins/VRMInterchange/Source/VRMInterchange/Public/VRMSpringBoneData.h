#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VRMSpringBonesTypes.h"
#include "VRMSpringBoneData.generated.h"

// Runtime-available data asset storing parsed spring bone configuration.
// Moved from Editor module to allow packaged builds to access spring settings.
UCLASS(BlueprintType)
class VRMINTERCHANGE_API UVRMSpringBoneData : public UDataAsset
{
    GENERATED_BODY()
public:
    // Show inner struct fields directly so users can inspect arrays like Colliders/Joints/Springs
    UPROPERTY(EditAnywhere, Category="Spring Bones", meta=(ShowOnlyInnerProperties))
    FVRMSpringConfig SpringConfig;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceHash;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceFilename;
};
