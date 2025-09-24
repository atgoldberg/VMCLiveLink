// Minimal module entry point for the editor module.
#include "Modules/ModuleManager.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "InterchangeManager.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Class.h"
#include "InterchangePipelineBase.h"

// NEW: programmatic pipeline stack append
#include "InterchangeProjectSettings.h"
#include "VRMTranslator.h"

namespace
{
#if WITH_EDITOR
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

	// Append our pipeline (prefer asset so settings like bGenerateSpringBoneData are honored)
	// Fallback to class path if the asset does not exist.
	const FSoftObjectPath SpringAssetPath(TEXT("/VRMInterchange/DefaultPipelines/DefaultSpringBonesPipeline.DefaultSpringBonesPipeline"));
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
	// Try to add the asset path first
	if (!ContainsPath(SpringAssetPath))
	{
		Per->Pipelines.Add(SpringAssetPath);
		bDirty = true;
	}

	// As a safety, if asset fails to load at runtime the engine will still show the path; optionally add class path as backup if not present
	if (!ContainsPath(SpringClassPath))
	{
		// Keep only asset by default; comment next line if you do not want a backup class reference
		// Per->Pipelines.Add(SpringClassPath);
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

        // Append our pipeline to the project settings (safe, non-destructive)
        AppendVRMSpringBonesPipeline();
#endif
    }

    virtual void ShutdownModule() override
    {
        UE_LOG(LogTemp, Log, TEXT("VRMInterchangeEditor module shutdown"));
    }
};

IMPLEMENT_MODULE(FVRMInterchangeEditorModule, VRMInterchangeEditor)