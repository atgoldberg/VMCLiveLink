#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VRMSpringBonesTypes.h"
#include "VRMSpringBoneData.generated.h"

// Editor-only data asset that stores parsed spring bone configuration,
// plus optional provenance information for regeneration/debugging.
UCLASS(BlueprintType)
class VRMINTERCHANGEEDITOR_API UVRMSpringBoneData : public UDataAsset
{
    GENERATED_BODY()
public:
    // Parsed and normalized spring bone configuration (runtime-shared type)
    UPROPERTY(EditAnywhere, Category="Spring Bones")
    FVRMSpringConfig SpringConfig;

    // Optional source info for staleness checks / diagnostics
    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceHash;

    UPROPERTY(VisibleAnywhere, Category="Spring Bones")
    FString SourceFilename;
};