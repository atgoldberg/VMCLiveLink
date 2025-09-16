// Copyright (c) 2025 Shocap Entertainment | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkSettings.h"
#include "VMCLiveLinkRemapper.h"

UVMCLiveLinkSettings::UVMCLiveLinkSettings()
{
    // helps where it appears in the Settings tree (optional)
    CategoryName = TEXT("Plugins");
    SectionName = TEXT("VMC Live Link");

    // ✅ Set a default TYPE so the Project Settings field isn’t None
    DefaultRemapperClass = UVMCLiveLinkRemapper::StaticClass();
}
