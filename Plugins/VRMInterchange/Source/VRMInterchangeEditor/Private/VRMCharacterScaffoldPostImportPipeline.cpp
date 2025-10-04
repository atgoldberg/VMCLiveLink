#include "VRMCharacterScaffoldPostImportPipeline.h"

#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Components/SkeletalMeshComponent.h"
#include "VRMInterchangeSettings.h"

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Subsystems/ImportSubsystem.h"
#endif

// Stage names/paths and defer creation to post-import (after dialog confirmation)
void UVRMCharacterScaffoldPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
#if WITH_EDITOR
	Super::ExecutePipeline(BaseNodeContainer, SourceDatas, ContentBasePath);

	// Project setting gate
	if (const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>())
	{
		if (!Settings->bGenerateLiveLinkEnabledActor)
		{
			return;
		}
	}

	if(!bGenerateScaffold) return;
	if(!BaseNodeContainer) return;

	const UInterchangeSourceData* Source=nullptr;
	for(const UInterchangeSourceData* SD:SourceDatas){ if(SD){ Source=SD; break; }}
	if(!Source) return;

	const FString Filename = Source->GetFilename();
	const FString CharacterBasePath = MakeCharacterBasePath(Filename, ContentBasePath);
	DeferredPackagePath = CharacterBasePath;
	DeferredSkeletonSearchRoot = CharacterBasePath;
	DeferredAltSkeletonSearchRoot = GetParentPackagePath(CharacterBasePath);

	const FString CharacterName = FPaths::GetBaseFilename(Filename);
	// LiveLink folder root
	const FString LiveLinkFolder = CharacterBasePath / TEXT("LiveLink");
	const FString AnimFolder = LiveLinkFolder / AnimationSubFolder;

	// Cache desired asset names and locations for post-import creation
	DeferredActorBPPath = LiveLinkFolder;
	DeferredAnimBPPath = AnimFolder;
	DeferredActorBPName = FString::Printf(TEXT("BP_VRM_%s"), *CharacterName);
	DeferredAnimBPName  = FString::Printf(TEXT("ABP_VRM_%s"), *CharacterName);
	bDeferredOverwrite = bOverwriteExisting;

	// Do NOT duplicate here; defer until after import is confirmed and skeletal assets exist
	RegisterPostImportCommit();
#endif
}

#if WITH_EDITOR
void UVRMCharacterScaffoldPostImportPipeline::BeginDestroy()
{
	UnregisterPostImportCommit();
	Super::BeginDestroy();
}
#endif

#if WITH_EDITOR

bool UVRMCharacterScaffoldPostImportPipeline::FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const
{
	OutSkeletalMesh=nullptr; OutSkeleton=nullptr; if(SearchRootPackagePath.IsEmpty()) return false; FAssetRegistryModule& ARM=FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry"); FARFilter MeshFilter; MeshFilter.bRecursivePaths=true; MeshFilter.PackagePaths.Add(*SearchRootPackagePath); MeshFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName()); TArray<FAssetData> Meshes; ARM.Get().GetAssets(MeshFilter, Meshes); if(Meshes.Num()>0){ OutSkeletalMesh=Cast<USkeletalMesh>(Meshes[0].GetAsset()); if(OutSkeletalMesh) OutSkeleton=OutSkeletalMesh->GetSkeleton(); } if(!OutSkeleton){ FARFilter SkelFilter; SkelFilter.bRecursivePaths=true; SkelFilter.PackagePaths.Add(*SearchRootPackagePath); SkelFilter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName()); TArray<FAssetData> Skels; ARM.Get().GetAssets(SkelFilter, Skels); if(Skels.Num()>0) OutSkeleton=Cast<USkeleton>(Skels[0].GetAsset()); } return (OutSkeletalMesh!=nullptr)||(OutSkeleton!=nullptr);
}

FString UVRMCharacterScaffoldPostImportPipeline::GetParentPackagePath(const FString& InPath) const
{ int32 SlashIdx=INDEX_NONE; return (InPath.FindLastChar(TEXT('/'),SlashIdx)&&SlashIdx>1)?InPath.Left(SlashIdx):InPath; }

FString UVRMCharacterScaffoldPostImportPipeline::MakeCharacterBasePath(const FString& SourceFilename, const FString& ContentBasePath) const
{ const FString BaseName = FPaths::GetBaseFilename(SourceFilename); return !ContentBasePath.IsEmpty() ? (ContentBasePath / BaseName) : FString::Printf(TEXT("/Game/%s"), *BaseName); }

UObject* UVRMCharacterScaffoldPostImportPipeline::DuplicateTemplate(const TCHAR* TemplatePath, const FString& TargetPackagePath, const FString& DesiredName, bool bOverwrite) const
{
	if(!TemplatePath||TargetPackagePath.IsEmpty()||DesiredName.IsEmpty()) return nullptr; UObject* TemplateObj = StaticLoadObject(UObject::StaticClass(), nullptr, TemplatePath); if(!TemplateObj) return nullptr; FString NewAssetPath = TargetPackagePath / DesiredName; FAssetToolsModule& AssetToolsModule=FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools"); FString UniquePath,UniqueName; if(!bOverwrite) { AssetToolsModule.Get().CreateUniqueAssetName(NewAssetPath,TEXT(""),UniquePath,UniqueName);} else { UniquePath=NewAssetPath; UniqueName=DesiredName; } const FString LongPackage = UniquePath.StartsWith(TEXT("/"))?UniquePath:TEXT("/")+UniquePath; UPackage* Pkg=CreatePackage(*LongPackage); if(!Pkg) return nullptr; UObject* Duplicated=StaticDuplicateObject(TemplateObj,Pkg,*UniqueName); if(!Duplicated) return nullptr; FAssetRegistryModule::AssetCreated(Duplicated); if(UBlueprint* BP=Cast<UBlueprint>(Duplicated)) { FKismetEditorUtilities::CompileBlueprint(BP); } return Duplicated; }

bool UVRMCharacterScaffoldPostImportPipeline::AssignSkeletalMeshToActorBP(UObject* ActorBlueprintObj, USkeletalMesh* SkeletalMesh) const
{
	UBlueprint* BP = Cast<UBlueprint>(ActorBlueprintObj); if(!BP||!SkeletalMesh) return false; if(!BP->GeneratedClass){ FKismetEditorUtilities::CompileBlueprint(BP);} UClass* GenClass=BP->GeneratedClass; if(!GenClass) return false; UObject* CDO=GenClass->GetDefaultObject(); if(!CDO) return false; USkeletalMeshComponent* FoundComp=nullptr; for (TObjectPtr<UActorComponent> Comp : CastChecked<AActor>(CDO)->GetComponents()) { if(USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(Comp)) { FoundComp = SKC; break; }} if(!FoundComp){ return false; } FoundComp->SetSkeletalMesh(SkeletalMesh); FoundComp->MarkPackageDirty(); BP->MarkPackageDirty(); return true; }

bool UVRMCharacterScaffoldPostImportPipeline::AssignAnimBPToActorBP(UObject* ActorBlueprintObj, UAnimBlueprint* AnimBP) const
{
	if(!ActorBlueprintObj||!AnimBP) return false; if(!AnimBP->GeneratedClass){ FKismetEditorUtilities::CompileBlueprint(AnimBP);} UClass* AnimClass=AnimBP->GeneratedClass; if(!AnimClass) return false; UBlueprint* BP=Cast<UBlueprint>(ActorBlueprintObj); if(!BP||!BP->GeneratedClass){ return false; } UObject* CDO=BP->GeneratedClass->GetDefaultObject(); if(!CDO) return false; USkeletalMeshComponent* FoundComp=nullptr; for (TObjectPtr<UActorComponent> Comp : CastChecked<AActor>(CDO)->GetComponents()) { if(USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(Comp)) { FoundComp = SKC; break; }} if(!FoundComp) return false; FoundComp->SetAnimInstanceClass(AnimClass); FoundComp->MarkPackageDirty(); BP->MarkPackageDirty(); AnimBP->MarkPackageDirty(); return true; }

bool UVRMCharacterScaffoldPostImportPipeline::SetPreviewMeshOnAnimBP(UAnimBlueprint* AnimBP, USkeletalMesh* SkeletalMesh) const
{
	if(!AnimBP||!SkeletalMesh) return false; AnimBP->SetPreviewMesh(SkeletalMesh); AnimBP->MarkPackageDirty(); return true; }

void UVRMCharacterScaffoldPostImportPipeline::RegisterPostImportCommit()
{
	if (ImportPostHandle.IsValid())
	{
		return;
	}
	if (UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr)
	{
		ImportPostHandle = ImportSubsystem->OnAssetPostImport.AddUObject(this, &UVRMCharacterScaffoldPostImportPipeline::OnAssetPostImport);
	}
}

void UVRMCharacterScaffoldPostImportPipeline::UnregisterPostImportCommit()
{
	if (ImportPostHandle.IsValid())
	{
		if (UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr)
		{
			ImportSubsystem->OnAssetPostImport.Remove(ImportPostHandle);
		}
		ImportPostHandle.Reset();
	}
}

void UVRMCharacterScaffoldPostImportPipeline::OnAssetPostImport(UFactory* InFactory, UObject* InCreatedObject)
{
	if (bDeferredCompleted || !InCreatedObject)
	{
		return;
	}

	const bool bIsSkelMesh = InCreatedObject->IsA<USkeletalMesh>();
	const bool bIsSkeleton = InCreatedObject->IsA<USkeleton>();
	if (!bIsSkelMesh && !bIsSkeleton)
	{
		return;
	}

	const FString PkgPath = InCreatedObject->GetOutermost()->GetPathName();
	if (!PkgPath.StartsWith(DeferredSkeletonSearchRoot) && !PkgPath.StartsWith(DeferredAltSkeletonSearchRoot))
	{
		return;
	}

	// Resolve skeletal assets
	USkeletalMesh* SkelMesh=nullptr; USkeleton* Skeleton=nullptr;
	bool bFound=FindImportedSkeletalAssets(DeferredSkeletonSearchRoot,SkelMesh,Skeleton)&&(SkelMesh||Skeleton);
	if(!bFound) bFound=FindImportedSkeletalAssets(DeferredAltSkeletonSearchRoot,SkelMesh,Skeleton)&&(SkelMesh||Skeleton);
	if(!bFound) return;

	// Create assets now, then wire them up
	UBlueprint* ActorBP = Cast<UBlueprint>(DuplicateTemplate(TEXT("/VRMInterchange/BP_VRM_Template.BP_VRM_Template"), DeferredActorBPPath, DeferredActorBPName, bDeferredOverwrite));
	UAnimBlueprint* AnimBP  = Cast<UAnimBlueprint>(DuplicateTemplate(TEXT("/VRMInterchange/Animation/ABP_VRM_Template.ABP_VRM_Template"), DeferredAnimBPPath, DeferredAnimBPName, bDeferredOverwrite));

	if (SkelMesh && AnimBP)
	{
		SetPreviewMeshOnAnimBP(AnimBP, SkelMesh);
	}
	if (ActorBP && SkelMesh)
	{
		AssignSkeletalMeshToActorBP(ActorBP, SkelMesh);
		if (AnimBP) AssignAnimBPToActorBP(ActorBP, AnimBP);
	}

	// Leave packages dirty; let editor Save/SCC handle persistence
	if (AnimBP)  { AnimBP->MarkPackageDirty(); }
	if (ActorBP) { ActorBP->MarkPackageDirty(); }

	bDeferredCompleted = true;
	UnregisterPostImportCommit();
}

#endif
