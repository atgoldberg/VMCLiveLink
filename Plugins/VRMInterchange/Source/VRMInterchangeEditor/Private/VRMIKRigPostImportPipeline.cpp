#include "VRMIKRigPostImportPipeline.h"

#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Modules/ModuleManager.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Rig/IKRigDefinition.h"
#include "VRMInterchangeSettings.h" // new settings include

void UVRMIKRigPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
#if WITH_EDITOR
	// Global project setting gate
	if (const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>())
	{
		if (!Settings->bGenerateIKRigAssets)
		{
			return; // disabled at project level
		}
	}
	// Per-pipeline instance toggle
	if (!bGenerateIKRig)
	{
		return;
	}

	if (!BaseNodeContainer)
	{
		return;
	}

	const UInterchangeSourceData* Source = nullptr;
	for (const UInterchangeSourceData* SD : SourceDatas)
	{
		if (SD) { Source = SD; break; }
	}
	if (!Source)
	{
		return;
	}

	const FString Filename = Source->GetFilename();

	// Character base path: /Game/<CharacterName>
	const FString CharacterBasePath = MakeCharacterBasePath(Filename, ContentBasePath);
	DeferredPackagePath = CharacterBasePath;
	const FString AnimFolder = CharacterBasePath / AnimationSubFolder;
	const FString CharacterName = FPaths::GetBaseFilename(Filename);
	const FString DesiredIKName = FString::Printf(TEXT("IK_Rig_VRM_%s"), *CharacterName);

	USkeletalMesh* SkelMesh=nullptr; USkeleton* Skeleton=nullptr;
	const bool bFoundHere = FindImportedSkeletalAssets(CharacterBasePath, SkelMesh, Skeleton) && (SkelMesh || Skeleton);
	const bool bFoundParent = !bFoundHere && FindImportedSkeletalAssets(GetParentPackagePath(CharacterBasePath), SkelMesh, Skeleton) && (SkelMesh || Skeleton);
	if (!bFoundHere && !bFoundParent)
	{
		// No skeletal assets yet - register for deferral
		RegisterDeferredIKRig(CharacterBasePath, CharacterBasePath);
		DeferredAltSkeletonSearchRoot = GetParentPackagePath(CharacterBasePath);
		return;
	}

	UIKRigDefinition* NewIKRig = nullptr;
	if (!DuplicateTemplateIKRig(AnimFolder, DesiredIKName, NewIKRig, bOverwriteExisting))
	{
		return;
	}

	if (NewIKRig)
	{
		USkeletalMesh* TargetMesh = SkelMesh;
		if (!TargetMesh && Skeleton)
		{
			// best effort: find any mesh using this skeleton under character
			FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			FARFilter MeshFilter; MeshFilter.bRecursivePaths=true; MeshFilter.PackagePaths.Add(*CharacterBasePath); MeshFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
			TArray<FAssetData> Meshes; ARM.Get().GetAssets(MeshFilter, Meshes);
			if (Meshes.Num()>0) { TargetMesh = Cast<USkeletalMesh>(Meshes[0].GetAsset()); }
		}

		if (TargetMesh)
		{
			NewIKRig->SetPreviewMesh(TargetMesh, true);
			NewIKRig->MarkPackageDirty();
			if (UPackage* Pkg = NewIKRig->GetOutermost())
			{
				const FString FN = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
				FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone; SaveArgs.SaveFlags = SAVE_NoError;
				UPackage::SavePackage(Pkg, nullptr, *FN, SaveArgs);
			}
		}
	}
#endif
}

#if WITH_EDITOR

bool UVRMIKRigPostImportPipeline::FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const
{
	OutSkeletalMesh=nullptr; OutSkeleton=nullptr; if(SearchRootPackagePath.IsEmpty()) return false; FAssetRegistryModule& ARM=FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	FARFilter MeshFilter; MeshFilter.bRecursivePaths=true; MeshFilter.PackagePaths.Add(*SearchRootPackagePath); MeshFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
	TArray<FAssetData> Meshes; ARM.Get().GetAssets(MeshFilter, Meshes);
	if (Meshes.Num()>0){ OutSkeletalMesh=Cast<USkeletalMesh>(Meshes[0].GetAsset()); if(OutSkeletalMesh) OutSkeleton=OutSkeletalMesh->GetSkeleton(); }
	if(!OutSkeleton){ FARFilter SkelFilter; SkelFilter.bRecursivePaths=true; SkelFilter.PackagePaths.Add(*SearchRootPackagePath); SkelFilter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName()); TArray<FAssetData> Skels; ARM.Get().GetAssets(SkelFilter, Skels); if(Skels.Num()>0) OutSkeleton=Cast<USkeleton>(Skels[0].GetAsset()); }
	return (OutSkeletalMesh!=nullptr)||(OutSkeleton!=nullptr);
}

FString UVRMIKRigPostImportPipeline::GetParentPackagePath(const FString& InPath) const
{
	int32 SlashIdx=INDEX_NONE; return (InPath.FindLastChar(TEXT('/'),SlashIdx)&&SlashIdx>1)?InPath.Left(SlashIdx):InPath;
}

FString UVRMIKRigPostImportPipeline::MakeCharacterBasePath(const FString& SourceFilename, const FString& ContentBasePath) const
{
	const FString BaseName = FPaths::GetBaseFilename(SourceFilename);
	return !ContentBasePath.IsEmpty() ? (ContentBasePath / BaseName) : FString::Printf(TEXT("/Game/%s"), *BaseName);
}

bool UVRMIKRigPostImportPipeline::DuplicateTemplateIKRig(const FString& TargetPackagePath, const FString& BaseName, UIKRigDefinition*& OutIKRig, bool bOverwrite) const
{
	OutIKRig = nullptr;
	// Load template in plugin
	const TCHAR* TemplatePath = TEXT("/VRMInterchange/Animation/IK_Rig_VRMTemplate.IK_Rig_VRMTemplate");
	UIKRigDefinition* TemplateRig = Cast<UIKRigDefinition>(StaticLoadObject(UIKRigDefinition::StaticClass(), nullptr, TemplatePath));
	if (!TemplateRig)
	{
		UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] IK Rig pipeline: Could not find template IK Rig at '%s'."), TemplatePath);
		return false;
	}

	FString NewAssetPath = TargetPackagePath / BaseName;
	FAssetToolsModule& AssetToolsModule=FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	FString UniquePath, UniqueName;
	if (!bOverwrite)
	{
		AssetToolsModule.Get().CreateUniqueAssetName(NewAssetPath, TEXT(""), UniquePath, UniqueName);
	}
	else
	{
		UniquePath = NewAssetPath;
		UniqueName = BaseName;
	}

	const FString LongPackage = UniquePath.StartsWith(TEXT("/")) ? UniquePath : TEXT("/") + UniquePath;
	UPackage* Pkg = CreatePackage(*LongPackage);
	if (!Pkg) return false;

	UObject* Duplicated = StaticDuplicateObject(TemplateRig, Pkg, *UniqueName);
	if (!Duplicated) { UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] IK Rig pipeline: Failed to duplicate template IK Rig.")); return false; }
	FAssetRegistryModule::AssetCreated(Duplicated);
	OutIKRig = Cast<UIKRigDefinition>(Duplicated);
	return OutIKRig != nullptr;
}

void UVRMIKRigPostImportPipeline::RegisterDeferredIKRig(const FString& InSkeletonSearchRoot, const FString& InPackagePath)
{
	UnregisterDeferredIKRig();
	DeferredSkeletonSearchRoot = InSkeletonSearchRoot;
	DeferredAltSkeletonSearchRoot = GetParentPackagePath(InSkeletonSearchRoot);
	DeferredPackagePath = InPackagePath;
	bDeferredCompleted = false;
	FAssetRegistryModule& ARM=FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	DeferredHandle = ARM.Get().OnAssetAdded().AddUObject(this, &UVRMIKRigPostImportPipeline::OnAssetAddedForDeferredIKRig);
}

void UVRMIKRigPostImportPipeline::UnregisterDeferredIKRig()
{
	if (DeferredHandle.IsValid())
	{
		if (FModuleManager::Get().IsModuleLoaded("AssetRegistry"))
		{
			FAssetRegistryModule& ARM = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
			ARM.Get().OnAssetAdded().Remove(DeferredHandle);
		}
		DeferredHandle.Reset();
	}
}

void UVRMIKRigPostImportPipeline::OnAssetAddedForDeferredIKRig(const FAssetData& AssetData)
{
	if (bDeferredCompleted || !AssetData.IsValid()) return;
	const FName ClassName = AssetData.AssetClassPath.GetAssetName();
	const bool bIsSkeletalMesh = (ClassName == USkeletalMesh::StaticClass()->GetFName());
	const bool bIsSkeleton = (ClassName == USkeleton::StaticClass()->GetFName());
	if (!bIsSkeletalMesh && !bIsSkeleton) return;

	const FString PkgPath = AssetData.PackagePath.ToString();
	if (!PkgPath.StartsWith(DeferredSkeletonSearchRoot) && !PkgPath.StartsWith(DeferredAltSkeletonSearchRoot)) return;

	USkeletalMesh* SkelMesh=nullptr; USkeleton* Skeleton=nullptr;
	bool bFound = FindImportedSkeletalAssets(DeferredSkeletonSearchRoot, SkelMesh, Skeleton) && (SkelMesh || Skeleton);
	if (!bFound) bFound = FindImportedSkeletalAssets(DeferredAltSkeletonSearchRoot, SkelMesh, Skeleton) && (SkelMesh || Skeleton);
	if (!bFound) return;

	const FString CharacterName = FPaths::GetCleanFilename(DeferredPackagePath);
	const FString DesiredIKName = FString::Printf(TEXT("IK_Rig_VRM_%s"), *CharacterName);

	const FString AnimFolder = DeferredPackagePath / AnimationSubFolder;
	UIKRigDefinition* NewIKRig = nullptr;
	if (!DuplicateTemplateIKRig(AnimFolder, DesiredIKName, NewIKRig, bOverwriteExisting))
	{
		UnregisterDeferredIKRig();
		bDeferredCompleted = true;
		return;
	}

	if (NewIKRig && SkelMesh)
	{
		NewIKRig->SetPreviewMesh(SkelMesh, true);
		NewIKRig->MarkPackageDirty();
		if (UPackage* Pkg = NewIKRig->GetOutermost())
		{
			const FString FN = FPackageName::LongPackageNameToFilename(Pkg->GetName(), FPackageName::GetAssetPackageExtension());
			FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone; SaveArgs.SaveFlags = SAVE_NoError;
			UPackage::SavePackage(Pkg, nullptr, *FN, SaveArgs);
		}
	}

	UnregisterDeferredIKRig();
	bDeferredCompleted = true;
}

#endif
