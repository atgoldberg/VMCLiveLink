#pragma once
#include "Engine/DeveloperSettings.h"
#include "VMCLiveLinkSettings.generated.h"

class ULiveLinkSubjectRemapper;
class UVMCLiveLinkRemapper;
class USkeletalMesh;

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "VMC Live Link"))
class UVMCLiveLinkSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UVMCLiveLinkSettings(); // <-- add ctor

    // Pick a TYPE, not an asset
    UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (AllowAbstract = "false"))
    TSoftClassPtr<ULiveLinkSubjectRemapper> DefaultRemapperClass;

    // Optional convenience: a project-wide default ref mesh
    UPROPERTY(EditAnywhere, Config, Category = "Defaults")
    TSoftObjectPtr<USkeletalMesh> DefaultReferenceSkeleton;
};
