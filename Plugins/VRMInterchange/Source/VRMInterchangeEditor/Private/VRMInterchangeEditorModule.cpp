// Minimal module entry point for the editor module.
#include "VRMInterchangeEditorModule.h" // New header
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMIKRigPostImportPipeline.h"
#include "VRMCharacterScaffoldPostImportPipeline.h"
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

static UObject* FindPluginDefaultVRMIKRigPipelineAsset()
{
	const TCHAR* PluginObjectPath = TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMIKRigPipeline.DefaultVRMIKRigPipeline");
	if (UObject* Existing = StaticLoadObject(UVRMIKRigPostImportPipeline::StaticClass(), nullptr, PluginObjectPath))
	{
		return Existing;
	}
	return nullptr;
}

static UObject* FindPluginDefaultCharacterScaffoldPipelineAsset()
{
	const TCHAR* PluginObjectPath = TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMCharacterScaffoldPipeline.DefaultVRMCharacterScaffoldPipeline");
	if (UObject* Existing = StaticLoadObject(UVRMCharacterScaffoldPostImportPipeline::StaticClass(), nullptr, PluginObjectPath))
	{
		return Existing;
	}
	return nullptr;
}

static FInterchangeTranslatorPipelines* EnsurePerTranslator(UInterchangeProjectSettings& Settings, FInterchangeImportSettings& ImportSettings)
{
	FInterchangePipelineStack* AssetsStack = ImportSettings.PipelineStacks.Find(TEXT("Assets"));
	if (!AssetsStack) return nullptr;
	const FString TranslatorPath = UVRMTranslator::StaticClass()->GetPathName();
	for (FInterchangeTranslatorPipelines& It : AssetsStack->PerTranslatorPipelines)
	{
		if (It.Translator.ToSoftObjectPath().ToString() == TranslatorPath) { return &It; }
	}
	FInterchangeTranslatorPipelines NewEntry; NewEntry.Translator = UVRMTranslator::StaticClass(); NewEntry.Pipelines = AssetsStack->Pipelines; AssetsStack->PerTranslatorPipelines.Add(MoveTemp(NewEntry));
	return &AssetsStack->PerTranslatorPipelines.Last();
}

static void AppendPipelineIfMissing(FInterchangeTranslatorPipelines* Per, const FSoftObjectPath& Path, bool& bOutDirty)
{
	if (!Per) return; for (const FSoftObjectPath& Existing : Per->Pipelines){ if (Existing.ToString()==Path.ToString()) return; } Per->Pipelines.Add(Path); bOutDirty=true;
}

static void AppendVRMSpringBonesPipeline()
{
	UInterchangeProjectSettings* Settings = GetMutableDefault<UInterchangeProjectSettings>();
	if (!Settings) return;
	FInterchangeImportSettings& ImportSettings = FInterchangeProjectSettingsUtils::GetMutableImportSettings(*Settings, false);
	FInterchangeTranslatorPipelines* Per = EnsurePerTranslator(*Settings, ImportSettings);
	if (!Per) return;
	bool bDirty=false;
	if (UObject* PluginPipeline=FindPluginDefaultSpringBonesPipelineAsset())
	{
		AppendPipelineIfMissing(Per, FSoftObjectPath(PluginPipeline), bDirty);
	}
	else
	{
		AppendPipelineIfMissing(Per, FSoftObjectPath(TEXT("/Script/VRMInterchangeEditor.VRMSpringBonesPostImportPipeline")), bDirty);
	}
	if (bDirty) Settings->SaveConfig();
}

static void AppendVRMIKRigPipeline()
{
	UInterchangeProjectSettings* Settings = GetMutableDefault<UInterchangeProjectSettings>(); if(!Settings) return; FInterchangeImportSettings& ImportSettings = FInterchangeProjectSettingsUtils::GetMutableImportSettings(*Settings,false); FInterchangeTranslatorPipelines* Per=EnsurePerTranslator(*Settings,ImportSettings); if(!Per) return; bool bDirty=false; if(UObject* PluginPipeline=FindPluginDefaultVRMIKRigPipelineAsset()) { AppendPipelineIfMissing(Per,FSoftObjectPath(PluginPipeline),bDirty);} else { AppendPipelineIfMissing(Per,FSoftObjectPath(TEXT("/Script/VRMInterchangeEditor.VRMIKRigPostImportPipeline")),bDirty);} if(bDirty) Settings->SaveConfig(); }

static void AppendVRMCharacterScaffoldPipeline()
{
	UInterchangeProjectSettings* Settings = GetMutableDefault<UInterchangeProjectSettings>(); if(!Settings) return; FInterchangeImportSettings& ImportSettings = FInterchangeProjectSettingsUtils::GetMutableImportSettings(*Settings,false); FInterchangeTranslatorPipelines* Per=EnsurePerTranslator(*Settings,ImportSettings); if(!Per) return; bool bDirty=false; if(UObject* PluginPipeline=FindPluginDefaultCharacterScaffoldPipelineAsset()) { AppendPipelineIfMissing(Per,FSoftObjectPath(PluginPipeline),bDirty);} else { AppendPipelineIfMissing(Per,FSoftObjectPath(TEXT("/Script/VRMInterchangeEditor.VRMCharacterScaffoldPostImportPipeline")),bDirty);} if(bDirty) Settings->SaveConfig(); }

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
	UVRMIKRigPostImportPipeline::StaticClass();
	UVRMCharacterScaffoldPostImportPipeline::StaticClass();
#if WITH_EDITOR
	AppendVRMSpringBonesPipeline();
	AppendVRMIKRigPipeline();
	AppendVRMCharacterScaffoldPipeline();
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