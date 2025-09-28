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
#include "UObject/SavePackage.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Engine/Blueprint.h"
#include "Components/SkeletalMeshComponent.h"
#include "VRMInterchangeSettings.h" // project settings

void UVRMCharacterScaffoldPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
#if WITH_EDITOR
	// Project setting gate
	if (const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>())
	{
		if (!Settings->bGenerateLiveLinkEnabledActor)
		{
			return; // disabled globally
		}
	}

	if(!bGenerateScaffold) return; // per-instance toggle
	if(!BaseNodeContainer) return;
	const UInterchangeSourceData* Source=nullptr; for(const UInterchangeSourceData* SD:SourceDatas){ if(SD){ Source=SD; break; }} if(!Source) return;
	const FString Filename = Source->GetFilename();
	const FString CharacterBasePath = MakeCharacterBasePath(Filename, ContentBasePath);
	DeferredPackagePath = CharacterBasePath; // for deferred
	const FString CharacterName = FPaths::GetBaseFilename(Filename);
	// Target names
	const FString ActorBPName = FString::Printf(TEXT("BP_VRM_%s"), *CharacterName);
	const FString AnimBPName  = FString::Printf(TEXT("ABP_VRM_%s"), *CharacterName);
	// LiveLink folder root
	const FString LiveLinkFolder = CharacterBasePath / TEXT("LiveLink");
	const FString AnimFolder = LiveLinkFolder / AnimationSubFolder;

	USkeletalMesh* SkelMesh=nullptr; USkeleton* Skeleton=nullptr;
	const bool bFoundHere = FindImportedSkeletalAssets(CharacterBasePath, SkelMesh, Skeleton) && (SkelMesh||Skeleton);
	const bool bFoundParent = !bFoundHere && FindImportedSkeletalAssets(GetParentPackagePath(CharacterBasePath), SkelMesh, Skeleton) && (SkelMesh||Skeleton);

	// Duplicate templates now (reserve names)
	UObject* ActorBPObj = DuplicateTemplate(TEXT("/VRMInterchange/BP_VRM_Template.BP_VRM_Template"), LiveLinkFolder, ActorBPName, bOverwriteExisting);
	UObject* AnimBPObj  = DuplicateTemplate(TEXT("/VRMInterchange/Animation/ABP_VRM_Template.ABP_VRM_Template"), AnimFolder, AnimBPName, bOverwriteExisting);
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(AnimBPObj);

	if(!bFoundHere && !bFoundParent)
	{
		// defer skeletal assignments
		DeferredActorBPPath = LiveLinkFolder; DeferredActorBPName = ActorBPName; DeferredAnimBPPath = AnimFolder; DeferredAnimBPName = AnimBPName; bDeferredOverwrite = bOverwriteExisting; RegisterDeferred(CharacterBasePath, CharacterBasePath); DeferredAltSkeletonSearchRoot = GetParentPackagePath(CharacterBasePath); return; }

	if(SkelMesh && AnimBP)
	{
		SetPreviewMeshOnAnimBP(AnimBP, SkelMesh);
	}
	if(ActorBPObj && SkelMesh)
	{
		AssignSkeletalMeshToActorBP(ActorBPObj, SkelMesh);
		if(AnimBP) AssignAnimBPToActorBP(ActorBPObj, AnimBP);
	}
#endif
}

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
	UBlueprint* BP = Cast<UBlueprint>(ActorBlueprintObj); if(!BP||!SkeletalMesh) return false; if(!BP->GeneratedClass){ FKismetEditorUtilities::CompileBlueprint(BP);} UClass* GenClass=BP->GeneratedClass; if(!GenClass) return false; UObject* CDO=GenClass->GetDefaultObject(); if(!CDO) return false; // find a USkeletalMeshComponent property named something common (e.g., SkeletalMeshComponent, Mesh, SkeletalMesh)
	USkeletalMeshComponent* FoundComp=nullptr; for (TObjectPtr<UActorComponent> Comp : CastChecked<AActor>(CDO)->GetComponents()) { if(USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(Comp)) { FoundComp = SKC; break; }} if(!FoundComp){ return false; } FoundComp->SetSkeletalMesh(SkeletalMesh); FoundComp->MarkPackageDirty(); BP->MarkPackageDirty(); return true; }

bool UVRMCharacterScaffoldPostImportPipeline::AssignAnimBPToActorBP(UObject* ActorBlueprintObj, UAnimBlueprint* AnimBP) const
{
	if(!ActorBlueprintObj||!AnimBP) return false; if(!AnimBP->GeneratedClass){ FKismetEditorUtilities::CompileBlueprint(AnimBP);} UClass* AnimClass=AnimBP->GeneratedClass; if(!AnimClass) return false; UBlueprint* BP=Cast<UBlueprint>(ActorBlueprintObj); if(!BP||!BP->GeneratedClass){ return false; } UObject* CDO=BP->GeneratedClass->GetDefaultObject(); if(!CDO) return false; USkeletalMeshComponent* FoundComp=nullptr; for (TObjectPtr<UActorComponent> Comp : CastChecked<AActor>(CDO)->GetComponents()) { if(USkeletalMeshComponent* SKC = Cast<USkeletalMeshComponent>(Comp)) { FoundComp = SKC; break; }} if(!FoundComp) return false; FoundComp->SetAnimInstanceClass(AnimClass); FoundComp->MarkPackageDirty(); BP->MarkPackageDirty(); AnimBP->MarkPackageDirty(); return true; }

bool UVRMCharacterScaffoldPostImportPipeline::SetPreviewMeshOnAnimBP(UAnimBlueprint* AnimBP, USkeletalMesh* SkeletalMesh) const
{
	if(!AnimBP||!SkeletalMesh) return false; AnimBP->SetPreviewMesh(SkeletalMesh); AnimBP->MarkPackageDirty(); return true; }

void UVRMCharacterScaffoldPostImportPipeline::RegisterDeferred(const FString& InSkeletonSearchRoot, const FString& InPackagePath)
{
	UnregisterDeferred(); DeferredSkeletonSearchRoot=InSkeletonSearchRoot; DeferredAltSkeletonSearchRoot=GetParentPackagePath(InSkeletonSearchRoot); DeferredPackagePath=InPackagePath; bDeferredCompleted=false; FAssetRegistryModule& ARM=FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry"); DeferredHandle=ARM.Get().OnAssetAdded().AddUObject(this,&UVRMCharacterScaffoldPostImportPipeline::OnAssetAddedDeferred); }

void UVRMCharacterScaffoldPostImportPipeline::UnregisterDeferred()
{
	if(DeferredHandle.IsValid()) { if(FModuleManager::Get().IsModuleLoaded("AssetRegistry")){ FAssetRegistryModule& ARM=FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry"); ARM.Get().OnAssetAdded().Remove(DeferredHandle);} DeferredHandle.Reset(); }
}

void UVRMCharacterScaffoldPostImportPipeline::OnAssetAddedDeferred(const FAssetData& AssetData)
{
	if(bDeferredCompleted||!AssetData.IsValid()) return; const FName ClassName=AssetData.AssetClassPath.GetAssetName(); const bool bIsSkeletalMesh=(ClassName==USkeletalMesh::StaticClass()->GetFName()); const bool bIsSkeleton=(ClassName==USkeleton::StaticClass()->GetFName()); if(!bIsSkeletalMesh&&!bIsSkeleton) return; const FString PkgPath=AssetData.PackagePath.ToString(); if(!PkgPath.StartsWith(DeferredSkeletonSearchRoot)&&!PkgPath.StartsWith(DeferredAltSkeletonSearchRoot)) return; USkeletalMesh* SkelMesh=nullptr; USkeleton* Skeleton=nullptr; bool bFound=FindImportedSkeletalAssets(DeferredSkeletonSearchRoot,SkelMesh,Skeleton)&&(SkelMesh||Skeleton); if(!bFound) bFound=FindImportedSkeletalAssets(DeferredAltSkeletonSearchRoot,SkelMesh,Skeleton)&&(SkelMesh||Skeleton); if(!bFound) return; // Load previously duplicated assets (in LiveLink folder structure)
	UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(StaticLoadObject(UAnimBlueprint::StaticClass(), nullptr, *(DeferredAnimBPPath + TEXT("/") + DeferredAnimBPName + TEXT(".") + DeferredAnimBPName)));
	UBlueprint* ActorBP = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *(DeferredActorBPPath + TEXT("/") + DeferredActorBPName + TEXT(".") + DeferredActorBPName)));
	if(SkelMesh && AnimBP){ SetPreviewMeshOnAnimBP(AnimBP, SkelMesh); }
	if(ActorBP && SkelMesh){ AssignSkeletalMeshToActorBP(ActorBP, SkelMesh); if(AnimBP) AssignAnimBPToActorBP(ActorBP, AnimBP); }
	if(AnimBP){ if(UPackage* P=AnimBP->GetOutermost()){ const FString FN=FPackageName::LongPackageNameToFilename(P->GetName(),FPackageName::GetAssetPackageExtension()); FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags=RF_Public|RF_Standalone; SaveArgs.SaveFlags=SAVE_NoError; UPackage::SavePackage(P,nullptr,*FN,SaveArgs);} }
	if(ActorBP){ if(UPackage* P=ActorBP->GetOutermost()){ const FString FN=FPackageName::LongPackageNameToFilename(P->GetName(),FPackageName::GetAssetPackageExtension()); FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags=RF_Public|RF_Standalone; SaveArgs.SaveFlags=SAVE_NoError; UPackage::SavePackage(P,nullptr,*FN,SaveArgs);} }
	UnregisterDeferred(); bDeferredCompleted=true; }

#endif
