// Editor-only post-import reparent: MI_VRM_<Character>_<Mat> -> MI_VRM_<Character>
// Robust to underscores; matches by prefix within the same folder.
// Hooks both UImportSubsystem::OnAssetPostImport and AssetRegistry::OnAssetAdded.
#include "CoreMinimal.h"
#include "Engine/Engine.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Subsystems/ImportSubsystem.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstance.h"
#include "Materials/Material.h"
#include "Misc/CoreDelegates.h"
#include "AssetRegistry/AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogVRMReparent, Log, All);

namespace
{
	static FString GetFolderPackagePath(const UObject* Obj)
	{
		if (!Obj) return TEXT("");
		const FString PkgName = Obj->GetOutermost()->GetName(); // e.g. /Game/Base/Materials/MI_VRM_...
		int32 SlashIdx = INDEX_NONE;
		if (PkgName.FindLastChar(TEXT('/'), SlashIdx) && SlashIdx > 0)
		{
			return PkgName.Left(SlashIdx); // e.g. /Game/Base/Materials
		}
		return TEXT("");
	}

	static bool IsVRMMIName(const FString& Name)
	{
		return Name.StartsWith(TEXT("MI_VRM_"));
	}

	// Resolve the ultimate master UMaterial at the root of an MI chain (or nullptr if unresolved).
	static const UMaterial* GetMasterMaterialOf(const UMaterialInterface* MatIf)
	{
		const UMaterialInterface* Cur = MatIf;
		int32 Guard = 32; // avoid pathological cycles
		while (Cur && Guard-- > 0)
		{
			if (const UMaterial* AsMat = Cast<UMaterial>(Cur))
			{
				return AsMat;
			}
			if (const UMaterialInstance* AsMI = Cast<UMaterialInstance>(Cur))
			{
				Cur = AsMI->Parent;
			}
			else
			{
				break;
			}
		}
		return nullptr;
	}

	struct FVRMPostImportReparent
	{
		FVRMPostImportReparent()
		{
			FCoreDelegates::OnPostEngineInit.AddRaw(this, &FVRMPostImportReparent::OnPostEngineInit);
			if (GEngine)
			{
				OnPostEngineInit();
			}
		}

		~FVRMPostImportReparent()
		{
			if (AssetPostImportHandle.IsValid())
			{
				if (GEditor)
				{
					if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
					{
						ImportSubsystem->OnAssetPostImport.Remove(AssetPostImportHandle);
					}
				}
			}
			AssetPostImportHandle.Reset();

			if (AssetAddedHandle.IsValid())
			{
				if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
				{
					FAssetRegistryModule& ARM = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
					ARM.Get().OnAssetAdded().Remove(AssetAddedHandle);
				}
			}
			AssetAddedHandle.Reset();

			MIsByFolder.Empty();
		}

		void OnPostEngineInit()
		{
			if (!GIsEditor || !GEditor) return;

			if (UImportSubsystem* ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
			{
				if (!AssetPostImportHandle.IsValid())
				{
					AssetPostImportHandle = ImportSubsystem->OnAssetPostImport.AddRaw(this, &FVRMPostImportReparent::OnAssetPostImport);
					UE_LOG(LogVRMReparent, Verbose, TEXT("[VRM] Subscribed to OnAssetPostImport."));
				}
			}

			if (!AssetAddedHandle.IsValid())
			{
				FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
				AssetAddedHandle = ARM.Get().OnAssetAdded().AddRaw(this, &FVRMPostImportReparent::OnAssetAdded);
				UE_LOG(LogVRMReparent, Verbose, TEXT("[VRM] Subscribed to AssetRegistry OnAssetAdded."));
			}
		}

		static void ReparentToCharacter(UMaterialInstanceConstant* Child, UMaterialInstanceConstant* CharacterMI)
		{
			if (!Child || !CharacterMI || Child->Parent == CharacterMI) return;

			// Same-master guard: only reparent if both resolve to the same ultimate master UMaterial.
			const UMaterial* ChildMaster = GetMasterMaterialOf(Child);
			const UMaterial* CharacterMaster = GetMasterMaterialOf(CharacterMI);
			if (ChildMaster == nullptr || CharacterMaster == nullptr || ChildMaster != CharacterMaster)
			{
				UE_LOG(LogVRMReparent, Verbose,
					TEXT("[VRM] Skip reparent '%s' -> '%s' (master mismatch: ChildMaster=%s, CharacterMaster=%s)"),
					*Child->GetName(),
					*CharacterMI->GetName(),
					ChildMaster ? *ChildMaster->GetName() : TEXT("null"),
					CharacterMaster ? *CharacterMaster->GetName() : TEXT("null"));
				return;
			}

			// Both are MIs under the same master; editor reparent preserves overrides.
			Child->SetParentEditorOnly(CharacterMI);
			Child->PostEditChange();
			Child->MarkPackageDirty();
			UE_LOG(LogVRMReparent, Log, TEXT("[VRM] Reparented '%s' -> '%s'"),
				*Child->GetName(), *CharacterMI->GetName());
		}

		// Try to resolve parent/child relationships for all MIs in this folder.
		void ResolveFolder(const FString& FolderPath)
		{
			TArray<TWeakObjectPtr<UMaterialInstanceConstant>>& List = MIsByFolder.FindOrAdd(FolderPath);

			TArray<UMaterialInstanceConstant*> Valid;
			Valid.Reserve(List.Num());
			for (const TWeakObjectPtr<UMaterialInstanceConstant>& W : List)
			{
				if (UMaterialInstanceConstant* P = W.Get())
				{
					Valid.Add(P);
				}
			}

			// For each pair, if ChildName starts with ParentName + "_", reparent.
			for (UMaterialInstanceConstant* Parent : Valid)
			{
				const FString ParentName = Parent->GetName();
				const FString Prefix = ParentName + TEXT("_");
				for (UMaterialInstanceConstant* Child : Valid)
				{
					if (Child == Parent) continue;
					if (Child->GetName().StartsWith(Prefix))
					{
						ReparentToCharacter(Child, Parent);
					}
				}
			}

			// Compact
			List.SetNum(Valid.Num());
			for (int32 i = 0; i < Valid.Num(); ++i)
			{
				List[i] = Valid[i];
			}
		}

		// ImportSubsystem callback
		void OnAssetPostImport(UFactory* /*InFactory*/, UObject* InCreatedObject)
		{
			UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(InCreatedObject);
			if (!MIC) return;

			const FString Name = MIC->GetName();
			if (!IsVRMMIName(Name)) return;

			const FString FolderPath = GetFolderPackagePath(MIC);
			if (FolderPath.IsEmpty()) return;

			UE_LOG(LogVRMReparent, Verbose, TEXT("[VRM] PostImport: %s in %s"), *Name, *FolderPath);

			TArray<TWeakObjectPtr<UMaterialInstanceConstant>>& List = MIsByFolder.FindOrAdd(FolderPath);
			List.Add(MIC);
			ResolveFolder(FolderPath);
		}

		// AssetRegistry fallback (covers assets created without broadcasting OnAssetPostImport)
		void OnAssetAdded(const FAssetData& AssetData)
		{
			if (!AssetData.IsValid()) return;
			if (AssetData.AssetClassPath != UMaterialInstanceConstant::StaticClass()->GetClassPathName()) return;

			const FString Name = AssetData.AssetName.ToString();
			if (!IsVRMMIName(Name)) return;

			UObject* Obj = AssetData.GetAsset();
			UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(Obj);
			if (!MIC) return;

			const FString FolderPath = GetFolderPackagePath(MIC);
			if (FolderPath.IsEmpty()) return;

			UE_LOG(LogVRMReparent, Verbose, TEXT("[VRM] AssetAdded: %s in %s"), *Name, *FolderPath);

			TArray<TWeakObjectPtr<UMaterialInstanceConstant>>& List = MIsByFolder.FindOrAdd(FolderPath);
			List.Add(MIC);
			ResolveFolder(FolderPath);
		}

	private:
		FDelegateHandle AssetPostImportHandle;
		FDelegateHandle AssetAddedHandle;
		TMap<FString, TArray<TWeakObjectPtr<UMaterialInstanceConstant>>> MIsByFolder;
	};

	static FVRMPostImportReparent GVRMPostImportReparent;
} // namespace
#endif // WITH_EDITOR