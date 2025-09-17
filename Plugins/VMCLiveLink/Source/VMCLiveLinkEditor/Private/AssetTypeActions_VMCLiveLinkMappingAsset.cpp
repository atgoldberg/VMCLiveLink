#include "AssetTypeActions_VMCLiveLinkMappingAsset.h"
#include "VMCLiveLinkMappingAsset.h"
#include "Modules/ModuleManager.h"

UClass* FAssetTypeActions_VMCLiveLinkMappingAsset::GetSupportedClass() const
{
	return UVMCLiveLinkMappingAsset::StaticClass();
}