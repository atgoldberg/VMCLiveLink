// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "VMCLiveLinkEditorModule.h"
#include "Modules/ModuleManager.h"
#include "IAssetTools.h"
#include "AssetToolsModule.h"
#include "AssetTypeActions_VMCLiveLinkMappingAsset.h"


#define LOCTEXT_NAMESPACE "FVMCLiveLinkEditorModule"

static EAssetTypeCategories::Type GVMCLiveLinkAssetCategory = EAssetTypeCategories::Misc;

namespace VMCLiveLinkEditor
{
	VMCLIVELINKEDITOR_API EAssetTypeCategories::Type GetAssetCategoryBit()
	{
		return GVMCLiveLinkAssetCategory;
	}
}

class FVMCLiveLinkEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		GVMCLiveLinkAssetCategory = AssetTools.RegisterAdvancedAssetCategory(TEXT("VMCLiveLink"), LOCTEXT("VMCLiveLinkCategory", "VMC LiveLink"));

		TSharedRef<FAssetTypeActions_Base> Action = MakeShared<FAssetTypeActions_VMCLiveLinkMappingAsset>();
		AssetTools.RegisterAssetTypeActions(Action);
		RegisteredActions.Add(Action);
#endif
	}

	virtual void ShutdownModule() override
	{
#if WITH_EDITOR
		if (FModuleManager::Get().IsModuleLoaded("AssetTools"))
		{
			IAssetTools& AssetTools = FModuleManager::GetModuleChecked<FAssetToolsModule>("AssetTools").Get();
			for (auto& Action : RegisteredActions)
			{
				AssetTools.UnregisterAssetTypeActions(Action);
			}
			RegisteredActions.Empty();
		}
#endif
	}

private:
	TArray<TSharedRef<FAssetTypeActions_Base>> RegisteredActions;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FVMCLiveLinkEditorModule, VMCLiveLinkEditor)
