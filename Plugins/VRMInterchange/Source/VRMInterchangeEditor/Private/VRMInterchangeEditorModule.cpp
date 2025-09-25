// Minimal module entry point for the editor module.
#include "VRMInterchangeEditorModule.h" // New header
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "InterchangeProjectSettings.h"
#include "VRMTranslator.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "VRMDeletedImportManager.h"
#include "VRMSpringBoneData.h"
#include "Editor.h"

namespace
{
#if WITH_EDITOR
static UObject* FindPluginDefaultSpringBonesPipelineAsset()
{
	const TCHAR* PluginObjectPath = TEXT("/VRMInterchange/DefaultPipelines/DefaultSpringBonesPipeline.DefaultSpringBonesPipeline");
	if (UObject* Existing = StaticLoadObject(UVRMSpringBonesPostImportPipeline::StaticClass(), nullptr, PluginObjectPath))
	{
		return Existing;
	}
	return nullptr;
}

static void AppendVRMSpringBonesPipeline()
{
	UInterchangeProjectSettings* Settings = GetMutableDefault<UInterchangeProjectSettings>();
	if (!Settings) return;
	FInterchangeImportSettings& ImportSettings = FInterchangeProjectSettingsUtils::GetMutableImportSettings(*Settings, false);
	FInterchangePipelineStack* AssetsStack = ImportSettings.PipelineStacks.Find(TEXT("Assets"));
	if (!AssetsStack) return;
	const FString TranslatorPath = UVRMTranslator::StaticClass()->GetPathName();
	FInterchangeTranslatorPipelines* Per = nullptr;
	for (FInterchangeTranslatorPipelines& It : AssetsStack->PerTranslatorPipelines)
	{
		if (It.Translator.ToSoftObjectPath().ToString() == TranslatorPath) { Per = &It; break; }
	}
	if (!Per)
	{
		FInterchangeTranslatorPipelines NewEntry; NewEntry.Translator = UVRMTranslator::StaticClass(); NewEntry.Pipelines = AssetsStack->Pipelines; AssetsStack->PerTranslatorPipelines.Add(MoveTemp(NewEntry)); Per = &AssetsStack->PerTranslatorPipelines.Last();
	}
	const FSoftObjectPath SpringClassPath(TEXT("/Script/VRMInterchangeEditor.VRMSpringBonesPostImportPipeline"));
	auto ContainsPath=[&](const FSoftObjectPath& P){ for(const FSoftObjectPath& Existing:Per->Pipelines){ if(Existing.ToString()==P.ToString()) return true;} return false; };
	bool bDirty=false;
	if (UObject* PluginPipeline=FindPluginDefaultSpringBonesPipelineAsset())
	{
		const FSoftObjectPath SpringAssetPath(PluginPipeline); if(!ContainsPath(SpringAssetPath)){ Per->Pipelines.Add(SpringAssetPath); bDirty=true; }
	}
	else if(!ContainsPath(SpringClassPath)) { Per->Pipelines.Add(SpringClassPath); bDirty=true; }
	if(bDirty) Settings->SaveConfig();
}

// Track hashes of spring data assets created this session but never (successfully) saved.
static TSet<FString> GUnsavedSpringDataHashes;

static void RegisterSpringDataCreation(UVRMSpringBoneData* Asset)
{
	if (Asset && !Asset->SourceHash.IsEmpty())
	{
		GUnsavedSpringDataHashes.Add(Asset->SourceHash);
	}
}

static void RegisterSpringDataSaved(UVRMSpringBoneData* Asset)
{
	if (Asset && !Asset->SourceHash.IsEmpty())
	{
		GUnsavedSpringDataHashes.Remove(Asset->SourceHash);
	}
}

static void HandlePreExit()
{
	if (GUnsavedSpringDataHashes.Num() > 0)
	{
		for (const FString& Hash : GUnsavedSpringDataHashes)
		{
			FVRMDeletedImportManager::Get().Add(Hash);
		}
		GUnsavedSpringDataHashes.Empty();
	}
}
#endif // WITH_EDITOR
}

IMPLEMENT_MODULE(FVRMInterchangeEditorModule, VRMInterchangeEditor)

// Module implementation
void FVRMInterchangeEditorModule::StartupModule()
{
	UVRMSpringBonesPostImportPipeline::StaticClass();
#if WITH_EDITOR
	AppendVRMSpringBonesPipeline();
	FCoreDelegates::OnPreExit.AddStatic(&HandlePreExit);
#endif
}

void FVRMInterchangeEditorModule::ShutdownModule()
{
#if WITH_EDITOR
	HandlePreExit();
#endif
}

#if WITH_EDITOR
void FVRMInterchangeEditorModule::NotifySpringDataCreated(UVRMSpringBoneData* Asset)
{
	RegisterSpringDataCreation(Asset);
}

void FVRMInterchangeEditorModule::NotifySpringDataSaved(UVRMSpringBoneData* Asset)
{
	RegisterSpringDataSaved(Asset);
}
#endif