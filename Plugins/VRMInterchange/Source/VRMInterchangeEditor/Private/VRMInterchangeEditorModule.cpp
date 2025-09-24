// Minimal module entry point for the editor module.
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "InterchangeManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "InterchangePipelineBase.h"

// NEW: programmatic pipeline stack append
#include "InterchangeProjectSettings.h"
#include "VRMTranslator.h"

// For asset lookup
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"

namespace
{
#if WITH_EDITOR
static UObject* FindPluginDefaultSpringBonesPipelineAsset()
{
	// Look for the pipeline asset shipped in the plugin content
	// Mount point for plugin content is "/VRMInterchange"
	const TCHAR* PluginObjectPath = TEXT("/VRMInterchange/DefaultPipelines/DefaultSpringBonesPipeline.DefaultSpringBonesPipeline");
	if (UObject* Existing = StaticLoadObject(UVRMSpringBonesPostImportPipeline::StaticClass(), nullptr, PluginObjectPath))
	{
		return Existing;
	}
	return nullptr;
}

static void AppendVRMSpringBonesPipeline()
{
	// Get mutable project settings (Content Import Settings)
	UInterchangeProjectSettings* Settings = GetMutableDefault<UInterchangeProjectSettings>();
	if (!Settings)
	{
		return;
	}

	// Work on Content (not Scene) import settings
	FInterchangeImportSettings& ImportSettings = FInterchangeProjectSettingsUtils::GetMutableImportSettings(*Settings, /*bIsSceneImport*/ false);

	// Only touch the "Assets" stack if it exists; do not create or override user stacks
	FInterchangePipelineStack* AssetsStack = ImportSettings.PipelineStacks.Find(TEXT("Assets"));
	if (!AssetsStack)
	{
		// Respect existing configs; do nothing if the user renamed/removed the default stack
		return;
	}

	// Find or create the per-translator entry for our VRM translator
	const FString TranslatorPath = UVRMTranslator::StaticClass()->GetPathName();
	FInterchangeTranslatorPipelines* Per = nullptr;
	for (FInterchangeTranslatorPipelines& It : AssetsStack->PerTranslatorPipelines)
	{
		if (It.Translator.ToSoftObjectPath().ToString() == TranslatorPath)
		{
			Per = &It;
			break;
		}
	}
	if (!Per)
	{
		FInterchangeTranslatorPipelines NewEntry;
		NewEntry.Translator = UVRMTranslator::StaticClass();
		// Start with the base stack pipelines to preserve defaults
		NewEntry.Pipelines = AssetsStack->Pipelines;
		AssetsStack->PerTranslatorPipelines.Add(MoveTemp(NewEntry));
		Per = &AssetsStack->PerTranslatorPipelines.Last();
	}

	// Prefer the plugin asset; if not found, fall back to class path
	const FSoftObjectPath SpringClassPath(TEXT("/Script/VRMInterchangeEditor.VRMSpringBonesPostImportPipeline"));

	auto ContainsPath = [&](const FSoftObjectPath& P)
	{
		for (const FSoftObjectPath& Existing : Per->Pipelines)
		{
			if (Existing.ToString() == P.ToString())
			{
				return true;
			}
		}
		return false;
	};

	bool bDirty = false;

	if (UObject* PluginPipeline = FindPluginDefaultSpringBonesPipelineAsset())
	{
		const FSoftObjectPath SpringAssetPath(PluginPipeline);
		if (!ContainsPath(SpringAssetPath))
		{
			Per->Pipelines.Add(SpringAssetPath);
			bDirty = true;
		}
	}
	else
	{
		// Safe fallback: add by class so import still works without the asset
		if (!ContainsPath(SpringClassPath))
		{
			Per->Pipelines.Add(SpringClassPath);
			bDirty = true;
		}
	}

	if (bDirty)
	{
		Settings->SaveConfig();
	}
}
#endif // WITH_EDITOR
}

class FVRMInterchangeEditorModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        // Ensure the pipeline class is linked into the module binary
        UVRMSpringBonesPostImportPipeline::StaticClass();

        UE_LOG(LogTemp, Log, TEXT("VRMInterchangeEditor module started"));

#if WITH_EDITOR
        // Enumerate all loaded UClasses derived from UInterchangePipelineBase
        const UClass* PipelineBase = UInterchangePipelineBase::StaticClass();
        int32 Found = 0;
        for (TObjectIterator<UClass> It; It; ++It)
        {
            UClass* C = *It;
            if (!C->HasAnyClassFlags(CLASS_Deprecated) && C->IsChildOf(PipelineBase))
            {
                ++Found;
                const bool bIsTarget = (C == UVRMSpringBonesPostImportPipeline::StaticClass());
                UE_LOG(LogTemp, Log, TEXT("Interchange Pipeline subclass found: %s%s"), *C->GetName(), bIsTarget ? TEXT("  <-- target") : TEXT(""));
            }
        }
        UE_LOG(LogTemp, Log, TEXT("Interchange Pipeline subclasses enumerated: %d"), Found);

        // Append our pipeline to the project settings (safe, prefers plugin asset, falls back to class)
        AppendVRMSpringBonesPipeline();
#endif
    }

    virtual void ShutdownModule() override
    {
        UE_LOG(LogTemp, Log, TEXT("VRMInterchangeEditor module shutdown"));
    }
};

IMPLEMENT_MODULE(FVRMInterchangeEditorModule, VRMInterchangeEditor)