// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSettings.h"
#include "VMCLiveLinkRemapper.h"
#include "Engine/SkeletalMesh.h"

UVMCLiveLinkSettings::UVMCLiveLinkSettings()
{
    // helps where it appears in the Settings tree (optional)
    CategoryName = TEXT("Plugins");
    SectionName = TEXT("VMC Live Link");

    // Sensible defaults
    DefaultRemapperClass = UVMCLiveLinkRemapper::StaticClass();
    // DefaultReferenceSkeleton left unset; project can configure it.
}
