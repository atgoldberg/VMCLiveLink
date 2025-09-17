#pragma once
#include "CoreMinimal.h"
#include "IAssetTools.h"

// Exported accessor for the VMCLiveLink asset category bit (registered at module startup)
namespace VMCLiveLinkEditor
{
	VMCLIVELINKEDITOR_API EAssetTypeCategories::Type GetAssetCategoryBit();
}