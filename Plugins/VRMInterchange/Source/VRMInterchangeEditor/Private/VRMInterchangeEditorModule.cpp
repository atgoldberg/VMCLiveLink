// Minimal module entry point for the editor module.
#include "VRMInterchangeEditorModule.h" // New header
#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMIKRigPostImportPipeline.h"
#include "VRMLiveLinkPostImportPipeline.h"
#include "InterchangeProjectSettings.h"
#include "VRMTranslator.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "VRMSpringBoneData.h"
#include "Editor.h"

namespace { }

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

static UObject* FindPluginDefaultLiveLinkPipelineAsset()
{
	// Try new asset name first (if plugin content updated)
	const TCHAR* NewPath = TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMLiveLinkPipeline.DefaultVRMLiveLinkPipeline");
	if (UObject* Existing = StaticLoadObject(UVRMLiveLinkPostImportPipeline::StaticClass(), nullptr, NewPath))
	{
		return Existing;
	}
	// Fallback to legacy asset path if it still exists in the plugin content
	const TCHAR* LegacyPath = TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMCharacterScaffoldPipeline.DefaultVRMCharacterScaffoldPipeline");
	if (UObject* Legacy = StaticLoadObject(UObject::StaticClass(), nullptr, LegacyPath))
	{
		return Legacy;
	}
	return nullptr;
}

// New: Locate the plugin's DefaultVRMAssetsPipeline asset (same folder as other pipelines).
static UObject* FindPluginDefaultVRMAssetsPipelineAsset()
{
	const TCHAR* PluginObjectPath = TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAssetsPipeline.DefaultVRMAssetsPipeline");
	if (UObject* Existing = StaticLoadObject(UObject::StaticClass(), nullptr, PluginObjectPath))
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

static void AppendVRMLiveLinkPipeline()
{
	UInterchangeProjectSettings* Settings = GetMutableDefault<UInterchangeProjectSettings>(); if(!Settings) return; FInterchangeImportSettings& ImportSettings = FInterchangeProjectSettingsUtils::GetMutableImportSettings(*Settings,false); FInterchangeTranslatorPipelines* Per=EnsurePerTranslator(*Settings,ImportSettings); if(!Per) return; bool bDirty=false; if(UObject* PluginPipeline=FindPluginDefaultLiveLinkPipelineAsset()) { AppendPipelineIfMissing(Per,FSoftObjectPath(PluginPipeline),bDirty);} else { AppendPipelineIfMissing(Per,FSoftObjectPath(TEXT("/Script/VRMInterchangeEditor.VRMLiveLinkPostImportPipeline")),bDirty);} if(bDirty) Settings->SaveConfig(); }

// New: Ensure the first pipeline in the VRM translator's Assets stack is DefaultVRMAssetsPipeline
static void EnsureVRMAssetsPipelineIsFirst()
{
	UInterchangeProjectSettings* Settings = GetMutableDefault<UInterchangeProjectSettings>();
	if (!Settings) return;

	FInterchangeImportSettings& ImportSettings = FInterchangeProjectSettingsUtils::GetMutableImportSettings(*Settings, false);
	FInterchangeTranslatorPipelines* Per = EnsurePerTranslator(*Settings, ImportSettings);
	if (!Per) return;

	bool bDirty = false;

	// Resolve desired path (prefer the plugin asset; fallback to soft path string)
	FSoftObjectPath DesiredPath;
	if (UObject* PluginPipeline = FindPluginDefaultVRMAssetsPipelineAsset())
	{
		DesiredPath = FSoftObjectPath(PluginPipeline);
	}
	else
	{
		DesiredPath = FSoftObjectPath(TEXT("/VRMInterchange/DefaultPipelines/DefaultVRMAssetsPipeline.DefaultVRMAssetsPipeline"));
	}

	// Remove any default Interchange Assets pipeline entries that UE may auto-insert
	{
		auto IsDefaultAssetsPipeline = [](const FSoftObjectPath& P)->bool
		{
			const FString S = P.ToString();
			return S.Contains(TEXT("DefaultAssetsPipeline"))
				|| S.Equals(TEXT("/Script/InterchangePipelines.InterchangeGenericAssetsPipeline"))
				|| S.Equals(TEXT("/Script/InterchangeEditor.InterchangeGenericAssetsPipeline"));
		};
		for (int32 i = Per->Pipelines.Num()-1; i >= 0; --i)
		{
			if (IsDefaultAssetsPipeline(Per->Pipelines[i]))
			{
				Per->Pipelines.RemoveAt(i);
				bDirty = true;
			}
		}
	}

	// If first differs, move or insert DesiredPath to index 0 and avoid duplicates.
	if (Per->Pipelines.Num() == 0)
	{
		Per->Pipelines.Add(DesiredPath);
		bDirty = true;
	}
	else if (Per->Pipelines[0].ToString() != DesiredPath.ToString())
	{
		int32 ExistingIndex = INDEX_NONE;
		for (int32 i = 0; i < Per->Pipelines.Num(); ++i)
		{
			if (Per->Pipelines[i].ToString() == DesiredPath.ToString())
			{
				ExistingIndex = i;
				break;
			}
		}
		if (ExistingIndex != INDEX_NONE)
		{
			Per->Pipelines.RemoveAt(ExistingIndex);
		}
		Per->Pipelines.Insert(DesiredPath, 0);
		bDirty = true;
	}

	// Ensure no duplicates of DesiredPath beyond index 0
	for (int32 i = Per->Pipelines.Num()-1; i > 0; --i)
	{
		if (Per->Pipelines[i].ToString() == DesiredPath.ToString())
		{
			Per->Pipelines.RemoveAt(i);
			bDirty = true;
		}
	}

	if (bDirty)
	{
		Settings->SaveConfig();
	}
}

// New: Ensure "Textures" dialog override includes VRMTranslator with both dialog flags enabled
static void EnsureVRMImportDialogOverrideForTextures()
{
	UInterchangeProjectSettings* Settings = GetMutableDefault<UInterchangeProjectSettings>();
	if (!Settings) return;

	bool bDirty = false;

	// Access the Content Import Settings map: Show Import Dialog Override -> Textures (enum key)
	auto& OverrideMap = Settings->ContentImportSettings.ShowImportDialogOverride;
	auto& TexturesOverride = OverrideMap.FindOrAdd(EInterchangeTranslatorAssetType::Textures);

	// Find or add per-translator override for VRMTranslator
	auto& PerTranslator = TexturesOverride.PerTranslatorImportDialogOverride;
	const FString VRMTranslatorPath = UVRMTranslator::StaticClass()->GetPathName();

	int32 FoundIndex = INDEX_NONE;
	for (int32 i = 0; i < PerTranslator.Num(); ++i)
	{
		if (PerTranslator[i].Translator.ToSoftObjectPath().ToString() == VRMTranslatorPath)
		{
			FoundIndex = i;
			break;
		}
	}

	if (FoundIndex == INDEX_NONE)
	{
		FoundIndex = PerTranslator.AddDefaulted();
		PerTranslator[FoundIndex].Translator = UVRMTranslator::StaticClass();
		bDirty = true;
	}

	// Ensure both flags are enabled
	if (!PerTranslator[FoundIndex].bShowImportDialog)
	{
		PerTranslator[FoundIndex].bShowImportDialog = true;
		bDirty = true;
	}
	if (!PerTranslator[FoundIndex].bShowReimportDialog)
	{
		PerTranslator[FoundIndex].bShowReimportDialog = true;
		bDirty = true;
	}

	if (bDirty)
	{
		Settings->SaveConfig();
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
	UVRMLiveLinkPostImportPipeline::StaticClass();
#if WITH_EDITOR
	AppendVRMSpringBonesPipeline();
	AppendVRMIKRigPipeline();
	AppendVRMLiveLinkPipeline();
	EnsureVRMAssetsPipelineIsFirst();

	// New: Ensure dialog override for VRM textures
	EnsureVRMImportDialogOverrideForTextures();
#endif
}

void FVRMInterchangeEditorModule::ShutdownModule()
{
#if WITH_EDITOR
    // HandlePreExit is already bound to FCoreDelegates::OnPreExit; don't call it again here.
#endif
}

#if WITH_EDITOR
void FVRMInterchangeEditorModule::NotifySpringDataCreated(UVRMSpringBoneData* /*Asset*/)
{
	// no-op
}

void FVRMInterchangeEditorModule::NotifySpringDataSaved(UVRMSpringBoneData* /*Asset*/)
{
	// no-op
}
#endif