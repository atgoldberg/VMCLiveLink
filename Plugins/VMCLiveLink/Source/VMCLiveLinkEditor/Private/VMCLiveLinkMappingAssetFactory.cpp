#include "VMCLiveLinkMappingAssetFactory.h"
#include "VMCLiveLinkMappingAsset.h"
#include "VMCLiveLinkEditorModule.h"

UVMCLiveLinkMappingAssetFactory::UVMCLiveLinkMappingAssetFactory()
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UVMCLiveLinkMappingAsset::StaticClass();
}

UObject* UVMCLiveLinkMappingAssetFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<UVMCLiveLinkMappingAsset>(InParent, Class, Name, Flags);
}

uint32 UVMCLiveLinkMappingAssetFactory::GetMenuCategories() const
{
	return VMCLiveLinkEditor::GetAssetCategoryBit();
}

FText UVMCLiveLinkMappingAssetFactory::GetDisplayName() const
{
	return NSLOCTEXT("VMCLiveLink", "VMCLiveLinkMappingAssetFactory", "VMC LiveLink Mapping Asset");
}