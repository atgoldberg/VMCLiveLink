#include "VMCLiveLinkSettings.h"
#include "LiveLinkAnimAndCurveRemapper.h"

UVMCLiveLinkSettings::UVMCLiveLinkSettings()
{
    // helps where it appears in the Settings tree (optional)
    CategoryName = TEXT("Plugins");
    SectionName = TEXT("VMC Live Link");

    // ✅ Set a default TYPE so the Project Settings field isn’t None
    DefaultRemapperClass = ULiveLinkAnimAndCurveRemapper::StaticClass();
}
