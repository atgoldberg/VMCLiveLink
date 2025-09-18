#pragma once

#include "AssetTypeActions_Base.h"

class FAssetTypeActions_VMCLiveLinkMappingAsset : public FAssetTypeActions_Base
{
public:
	// IAssetTypeActions
	virtual FText GetName() const override { return NSLOCTEXT("AssetTypeActions", "VMCLiveLinkMappingAsset", "VMC LiveLink Mapping Asset"); }
	virtual FColor GetTypeColor() const override { return FColor(0x00A3E8FF); }
	virtual UClass* GetSupportedClass() const override;
	virtual uint32 GetCategories() override;
	virtual bool IsImportedAsset() const override { return false; }
};