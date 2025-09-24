#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMSpringBoneData.h"
// Removed direct inclusion of module .cpp; declare module interface stub for static helper
#if WITH_EDITOR
class FVRMInterchangeEditorModule { public: static void NotifySpringDataCreated(class UVRMSpringBoneData* Asset); };
#endif
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "Misc/SecureHash.h"
#include "UObject/SavePackage.h"
#include "VRMSpringBonesParser.h"
#include "VRMInterchangeLog.h"
#include "VRMInterchangeSettings.h"
#include "VRMDeletedImportManager.h"

// cgltf
#if !defined(VRM_HAS_CGLTF)
#  if defined(__has_include)
#    if __has_include("cgltf.h")
#      define CGLTF_IMPLEMENTATION
#      include "cgltf.h"
#      define VRM_HAS_CGLTF 1
#    else
#      define VRM_HAS_CGLTF 0
#    endif
#  else
#    define VRM_HAS_CGLTF 0
#  endif
#endif

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Animation/AnimInstance.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR
void UVRMSpringBonesPostImportPipeline::PostInitProperties()
{
    Super::PostInitProperties();
    if (!HasAnyFlags(RF_ClassDefaultObject))
    {
        const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>();
        if (Settings)
        {
            bGenerateSpringBoneData     = Settings->bGenerateSpringBoneData;
            bOverwriteExisting          = Settings->bOverwriteExistingSpringAssets;
            bGeneratePostProcessAnimBP  = Settings->bGeneratePostProcessAnimBP;
            bAssignPostProcessABP       = Settings->bAssignPostProcessABP;
        }
    }
}
#endif

void UVRMSpringBonesPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
#if WITH_EDITOR
    const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>();
    const bool bWantsSpringData = (bGenerateSpringBoneData || (Settings && Settings->bGenerateSpringBoneData));
    const bool bWantsOverwrite  = (bOverwriteExisting || (Settings && Settings->bOverwriteExistingSpringAssets));

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
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No SourceData."));
        return;
    }

    const FString Filename = Source->GetFilename();

    FString PackagePath, SpringAssetName;
    MakeTargetPathAndName(Filename, ContentBasePath, PackagePath, SpringAssetName);

    const FString SkeletonSearchRoot = PackagePath;
    const FString ParentSearchRoot   = GetParentPackagePath(SkeletonSearchRoot);

    if (!DataAssetName.IsEmpty())
    {
        SpringAssetName = DataAssetName;
    }

    // Early hash for tombstone suppression
    FString SourceHash;
    if (FPaths::FileExists(Filename))
    {
        SourceHash = LexToString(FMD5Hash::HashFile(*Filename));
        if (FVRMDeletedImportManager::Get().Contains(SourceHash))
        {
            UE_LOG(LogVRMSpring, Log, TEXT("[VRMInterchange] Spring pipeline: Suppressing regeneration (tombstoned) for '%s' hash %s."), *Filename, *SourceHash);
            return;
        }
    }

    FString SpringDataFolder = PackagePath;
    if (!SubFolder.IsEmpty())
    {
        SpringDataFolder /= SubFolder;
    }

    FString FinalSpringPackageName = SpringDataFolder / SpringAssetName;
    if (!bWantsOverwrite)
    {
        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        AssetToolsModule.Get().CreateUniqueAssetName(FinalSpringPackageName, TEXT(""), FinalSpringPackageName, SpringAssetName);
    }

    FString LongSpringPackageName = FinalSpringPackageName;
    if (!LongSpringPackageName.StartsWith(TEXT("/")))
    {
        LongSpringPackageName = TEXT("/") + LongSpringPackageName;
    }

    UPackage* SpringPackage = CreatePackage(*LongSpringPackageName);
    if (!SpringPackage)
    {
        UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Failed to create/find spring package '%s'."), *LongSpringPackageName);
        return;
    }

    UVRMSpringBoneData* SpringDataAsset = nullptr;
    if (bWantsSpringData)
    {
        SpringDataAsset = FindObject<UVRMSpringBoneData>(SpringPackage, *SpringAssetName);
        bool bIsNew = false;
        if (!SpringDataAsset)
        {
            if (FindObject<UObject>(SpringPackage, *SpringAssetName))
            {
                UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Conflicting object '%s' exists in '%s'."), *SpringAssetName, *LongSpringPackageName);
                return;
            }
            SpringDataAsset = NewObject<UVRMSpringBoneData>(SpringPackage, *SpringAssetName, RF_Public | RF_Standalone);
            bIsNew = true;
#if WITH_EDITOR
            FVRMInterchangeEditorModule::NotifySpringDataCreated(SpringDataAsset);
#endif
        }

        if (!ParseAndFillDataAssetFromFile(Filename, SpringDataAsset))
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No spring data found in '%s'."), *Filename);
        }
        else
        {
            int32 ResolvedC=0, ResolvedJ=0, ResolvedCenters=0;
            ResolveBoneNamesFromFile(Filename, SpringDataAsset->SpringConfig, ResolvedC, ResolvedJ, ResolvedCenters);
            ValidateBoneNamesAgainstSkeleton(SkeletonSearchRoot, SpringDataAsset->SpringConfig);
            SpringDataAsset->SourceFilename = Filename;
            if (!SourceHash.IsEmpty())
            {
                SpringDataAsset->SourceHash = SourceHash;
                if (FVRMDeletedImportManager::Get().Contains(SourceHash))
                {
                    FVRMDeletedImportManager::Get().Remove(SourceHash); // manual reimport clears tombstone
                }
            }
            const TCHAR* SpecStr = TEXT("None");
            switch (SpringDataAsset->SpringConfig.Spec)
            {
                case EVRMSpringSpec::VRM0: SpecStr = TEXT("VRM0"); break;
                case EVRMSpringSpec::VRM1: SpecStr = TEXT("VRM1"); break;
                default: break;
            }
            UE_LOG(LogVRMSpring, Log, TEXT("[VRMInterchange] Spring pipeline: Parsed %s springs (hash=%s)."), SpecStr, *SpringDataAsset->SourceHash);
            if (bIsNew)
            {
                FAssetRegistryModule::AssetCreated(SpringDataAsset);
            }
        }

        SpringDataAsset->MarkPackageDirty();
        SpringPackage->SetDirtyFlag(true);
        const FString PackageFilename = FPackageName::LongPackageNameToFilename(SpringPackage->GetName(), FPackageName::GetAssetPackageExtension());
        FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone; SaveArgs.SaveFlags = SAVE_NoError;
        UPackage::SavePackage(SpringPackage, SpringDataAsset, *PackageFilename, SaveArgs);
    }

    const bool bWantsABP  = (bGeneratePostProcessAnimBP || (Settings && Settings->bGeneratePostProcessAnimBP));
    const bool bWantsAssign = (bAssignPostProcessABP || (Settings && Settings->bAssignPostProcessABP));

    if (bWantsABP)
    {
        USkeletalMesh* SkelMesh=nullptr; USkeleton* Skeleton=nullptr;
        const bool bFoundHere   = FindImportedSkeletalAssets(SkeletonSearchRoot, SkelMesh, Skeleton) && (SkelMesh || Skeleton);
        const bool bFoundParent = !bFoundHere && FindImportedSkeletalAssets(ParentSearchRoot, SkelMesh, Skeleton) && (SkelMesh || Skeleton);
        if (!bFoundHere && !bFoundParent)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: No skeletal assets under '%s' or parent '%s'. Deferring."), *SkeletonSearchRoot, *ParentSearchRoot);
            RegisterDeferredABP(SkeletonSearchRoot, PackagePath, SpringDataAsset, bWantsAssign);
            DeferredAltSkeletonSearchRoot = ParentSearchRoot;
        }
        else
        {
            const FString AnimFolder = PackagePath / AnimationSubFolder;
            UObject* DuplicatedABP = DuplicateTemplateAnimBlueprint(AnimFolder, TEXT("ABP_VRMSpringBones"), Skeleton ? Skeleton : (SkelMesh ? SkelMesh->GetSkeleton() : nullptr));
            if (DuplicatedABP && SpringDataAsset)
            {
                if (!SetSpringConfigOnAnimBlueprint(DuplicatedABP, SpringDataAsset))
                {
                    UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Failed to set SpringConfig on duplicated ABP."));
                }
            }
            if (DuplicatedABP && bWantsAssign && SkelMesh)
            {
                AssignPostProcessABPToMesh(SkelMesh, DuplicatedABP);
                if (UPackage* MeshPkg = SkelMesh->GetOutermost())
                {
                    const FString MeshPkgFilename = FPackageName::LongPackageNameToFilename(MeshPkg->GetName(), FPackageName::GetAssetPackageExtension());
                    FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone; SaveArgs.SaveFlags = SAVE_NoError;
                    UPackage::SavePackage(MeshPkg, SkelMesh, *MeshPkgFilename, SaveArgs);
                }
            }
            if (DuplicatedABP)
            {
                if (UObject* ABPOuter = DuplicatedABP->GetOutermost())
                {
                    if (UPackage* ABPPkg = Cast<UPackage>(ABPOuter))
                    {
                        const FString ABPPackageFilename = FPackageName::LongPackageNameToFilename(ABPPkg->GetName(), FPackageName::GetAssetPackageExtension());
                        FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags = RF_Public | RF_Standalone; SaveArgs.SaveFlags = SAVE_NoError;
                        UPackage::SavePackage(ABPPkg, nullptr, *ABPPackageFilename, SaveArgs);
                    }
                }
            }
        }
    }
#endif
}

bool UVRMSpringBonesPostImportPipeline::ParseAndFillDataAssetFromFile(const FString& Filename, UVRMSpringBoneData* Dest) const
{
    if (!Dest) return false;
    FVRMSpringConfig Config; FString Err;
    if (!VRM::ParseSpringBonesFromFile(Filename, Config, Err))
    {
        return false;
    }
    Dest->SpringConfig = MoveTemp(Config);
    return Dest->SpringConfig.IsValid();
}

FString UVRMSpringBonesPostImportPipeline::MakeTargetPathAndName(const FString& SourceFilename, const FString& ContentBasePath, FString& OutPackagePath, FString& OutAssetName) const
{
    const FString BaseName = FPaths::GetBaseFilename(SourceFilename);
    OutPackagePath = !ContentBasePath.IsEmpty() ? (ContentBasePath / BaseName) : FString::Printf(TEXT("/Game/%s"), *BaseName);
    OutAssetName = TEXT("SpringBonesData");
    return OutPackagePath / OutAssetName;
}

bool UVRMSpringBonesPostImportPipeline::ResolveBoneNamesFromFile(const FString& Filename, FVRMSpringConfig& InOut, int32& OutResolvedColliders, int32& OutResolvedJoints, int32& OutResolvedCenters) const
{
#if VRM_HAS_CGLTF
    OutResolvedColliders = OutResolvedJoints = OutResolvedCenters = 0;
    if (!InOut.IsValid()) return false;
    FTCHARToUTF8 PathUtf8(*Filename);
    cgltf_options Options = {}; cgltf_data* Data = nullptr;
    const cgltf_result Res = cgltf_parse_file(&Options, PathUtf8.Get(), &Data);
    if (Res != cgltf_result_success || !Data) { return false; }
    struct FScopedCgltf { cgltf_data* D; ~FScopedCgltf(){ if (D) cgltf_free(D); } } Scoped{ Data };
    const int32 NodesCount = static_cast<int32>(Data->nodes_count);
    auto GetNodeName = [&](int32 NodeIndex)->FName{ if(NodeIndex<0||NodeIndex>=NodesCount) return NAME_None; const cgltf_node* N=&Data->nodes[NodeIndex]; return (N&&N->name&&N->name[0])?FName(UTF8_TO_TCHAR(N->name)):NAME_None; };
    for (FVRMSpringCollider& C : InOut.Colliders) if (C.BoneName.IsNone() && C.NodeIndex!=INDEX_NONE){FName Nm=GetNodeName(C.NodeIndex); if(!Nm.IsNone()){C.BoneName=Nm; ++OutResolvedColliders;}}
    for (FVRMSpringJoint& J : InOut.Joints)    if (J.BoneName.IsNone() && J.NodeIndex!=INDEX_NONE){FName Nm=GetNodeName(J.NodeIndex); if(!Nm.IsNone()){J.BoneName=Nm; ++OutResolvedJoints;}}
    for (FVRMSpring& S : InOut.Springs)        if (S.CenterBoneName.IsNone() && S.CenterNodeIndex!=INDEX_NONE){FName Nm=GetNodeName(S.CenterNodeIndex); if(!Nm.IsNone()){S.CenterBoneName=Nm; ++OutResolvedCenters;}}
    return (OutResolvedColliders+OutResolvedJoints+OutResolvedCenters)>0;
#else
    OutResolvedColliders = OutResolvedJoints = OutResolvedCenters = 0; return false;
#endif
}

void UVRMSpringBonesPostImportPipeline::ValidateBoneNamesAgainstSkeleton(const FString& SearchRootPackagePath, const FVRMSpringConfig& Config) const
{
    if (SearchRootPackagePath.IsEmpty()) return;
    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    FARFilter SkelFilter; SkelFilter.bRecursivePaths=true; SkelFilter.PackagePaths.Add(*SearchRootPackagePath); SkelFilter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName());
    TArray<FAssetData> FoundSkeletons; ARM.Get().GetAssets(SkelFilter, FoundSkeletons);
    USkeleton* Skeleton = FoundSkeletons.Num()>0?Cast<USkeleton>(FoundSkeletons[0].GetAsset()):nullptr;
    if (!Skeleton)
    {
        FARFilter MeshFilter; MeshFilter.bRecursivePaths=true; MeshFilter.PackagePaths.Add(*SearchRootPackagePath); MeshFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
        TArray<FAssetData> Meshes; ARM.Get().GetAssets(MeshFilter, Meshes);
        if (Meshes.Num()>0) if (USkeletalMesh* SM=Cast<USkeletalMesh>(Meshes[0].GetAsset())) Skeleton=SM->GetSkeleton();
    }
    if (!Skeleton) return;
    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    TSet<FName> Valid; for(int32 i=0;i<RefSkel.GetNum();++i) Valid.Add(RefSkel.GetBoneName(i));
    auto Report=[&](const TArray<FName>& Missing,const TCHAR* What){ if(Missing.Num()>0) UE_LOG(LogVRMSpring,Warning,TEXT("[VRMInterchange] Spring pipeline: %d %s not on skeleton '%s'"),Missing.Num(),What,*Skeleton->GetPathName());};
    TArray<FName> MC, MJ, MCent;
    for(const FVRMSpringCollider& C:Config.Colliders) if(!C.BoneName.IsNone()&&!Valid.Contains(C.BoneName)) MC.AddUnique(C.BoneName);
    for(const FVRMSpringJoint& J:Config.Joints) if(!J.BoneName.IsNone()&&!Valid.Contains(J.BoneName)) MJ.AddUnique(J.BoneName);
    for(const FVRMSpring& S:Config.Springs) if(!S.CenterBoneName.IsNone()&&!Valid.Contains(S.CenterBoneName)) MCent.AddUnique(S.CenterBoneName);
    Report(MC,TEXT("collider BoneName(s)")); Report(MJ,TEXT("joint BoneName(s)")); Report(MCent,TEXT("center BoneName(s)"));
}

bool UVRMSpringBonesPostImportPipeline::FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const
{
    OutSkeletalMesh=nullptr; OutSkeleton=nullptr; if(SearchRootPackagePath.IsEmpty()) return false; FAssetRegistryModule& ARM=FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    FARFilter MeshFilter; MeshFilter.bRecursivePaths=true; MeshFilter.PackagePaths.Add(*SearchRootPackagePath); MeshFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
    TArray<FAssetData> Meshes; ARM.Get().GetAssets(MeshFilter, Meshes);
    if (Meshes.Num()>0){ OutSkeletalMesh=Cast<USkeletalMesh>(Meshes[0].GetAsset()); if(OutSkeletalMesh) OutSkeleton=OutSkeletalMesh->GetSkeleton(); }
    if(!OutSkeleton){ FARFilter SkelFilter; SkelFilter.bRecursivePaths=true; SkelFilter.PackagePaths.Add(*SearchRootPackagePath); SkelFilter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName()); TArray<FAssetData> Skels; ARM.Get().GetAssets(SkelFilter, Skels); if(Skels.Num()>0) OutSkeleton=Cast<USkeleton>(Skels[0].GetAsset()); }
    return (OutSkeletalMesh!=nullptr)||(OutSkeleton!=nullptr);
}

UObject* UVRMSpringBonesPostImportPipeline::DuplicateTemplateAnimBlueprint(const FString& TargetPackagePath, const FString& BaseName, USkeleton* TargetSkeleton) const
{
    if(!TargetSkeleton) return nullptr; const TCHAR* TemplatePath=TEXT("/VRMInterchange/Animation/ABP_VRMSpringBones_Template.ABP_VRMSpringBones_Template");
    UAnimBlueprint* TemplateABP=Cast<UAnimBlueprint>(StaticLoadObject(UAnimBlueprint::StaticClass(),nullptr,TemplatePath));
    if(!TemplateABP){ UE_LOG(LogVRMSpring,Warning,TEXT("[VRMInterchange] Spring pipeline: Could not find template AnimBlueprint at '%s'."),TemplatePath); return nullptr; }
    FString NewAssetPath=TargetPackagePath/ BaseName; FAssetToolsModule& AssetToolsModule=FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools"); FString UniquePath,UniqueName; AssetToolsModule.Get().CreateUniqueAssetName(NewAssetPath,TEXT(""),UniquePath,UniqueName);
    const FString LongPackage=UniquePath.StartsWith(TEXT("/"))?UniquePath:TEXT("/")+UniquePath; UPackage* Pkg=CreatePackage(*LongPackage); if(!Pkg) return nullptr;
    UObject* Duplicated=StaticDuplicateObject(TemplateABP,Pkg,*UniqueName); if(!Duplicated){ UE_LOG(LogVRMSpring,Warning,TEXT("[VRMInterchange] Spring pipeline: Failed to duplicate template ABP.")); return nullptr; }
    FAssetRegistryModule::AssetCreated(Duplicated);
    if(UAnimBlueprint* NewABP=Cast<UAnimBlueprint>(Duplicated)){ NewABP->TargetSkeleton=TargetSkeleton; FKismetEditorUtilities::CompileBlueprint(NewABP); }
    return Duplicated;
}

bool UVRMSpringBonesPostImportPipeline::SetSpringConfigOnAnimBlueprint(UObject* AnimBlueprintObj, UVRMSpringBoneData* SpringData) const
{
    if(!AnimBlueprintObj||!SpringData) return false; if(UAnimBlueprint* ABP=Cast<UAnimBlueprint>(AnimBlueprintObj)){ if(!ABP->GeneratedClass){ FKismetEditorUtilities::CompileBlueprint(ABP);} UAnimBlueprintGeneratedClass* GenClass=Cast<UAnimBlueprintGeneratedClass>(ABP->GeneratedClass); if(!GenClass) return false; UObject* CDO=GenClass->GetDefaultObject(); if(!CDO) return false; if(FObjectProperty* ObjProp=FindFProperty<FObjectProperty>(GenClass,TEXT("SpringConfig"))){ ObjProp->SetObjectPropertyValue_InContainer(CDO,SpringData);} else if(FSoftObjectProperty* SoftProp=FindFProperty<FSoftObjectProperty>(GenClass,TEXT("SpringConfig"))){ SoftProp->SetObjectPropertyValue_InContainer(CDO,SpringData);} else { return false; } CDO->Modify(); CDO->MarkPackageDirty(); ABP->MarkPackageDirty(); return true; }
    if(UClass* BPClass=Cast<UClass>(AnimBlueprintObj)){ UObject* CDO=BPClass->GetDefaultObject(); if(!CDO) return false; if(FObjectProperty* ObjProp=FindFProperty<FObjectProperty>(BPClass,TEXT("SpringConfig"))){ ObjProp->SetObjectPropertyValue_InContainer(CDO,SpringData); CDO->Modify(); CDO->MarkPackageDirty(); return true; } else if(FSoftObjectProperty* SoftProp=FindFProperty<FSoftObjectProperty>(BPClass,TEXT("SpringConfig"))){ SoftProp->SetObjectPropertyValue_InContainer(CDO,SpringData); CDO->Modify(); CDO->MarkPackageDirty(); return true; } }
    return false;
}

bool UVRMSpringBonesPostImportPipeline::AssignPostProcessABPToMesh(USkeletalMesh* SkelMesh, UObject* AnimBlueprintObj) const
{
    if(!SkelMesh||!AnimBlueprintObj) return false; UClass* BPClass=nullptr; if(UAnimBlueprint* ABP=Cast<UAnimBlueprint>(AnimBlueprintObj)){ FKismetEditorUtilities::CompileBlueprint(ABP); BPClass=ABP->GeneratedClass; } else { BPClass=Cast<UClass>(AnimBlueprintObj);} if(!BPClass) return false; SkelMesh->SetPostProcessAnimBlueprint(BPClass); SkelMesh->MarkPackageDirty(); return true;
}

FString UVRMSpringBonesPostImportPipeline::GetParentPackagePath(const FString& InPath) const
{ int32 SlashIdx=INDEX_NONE; return (InPath.FindLastChar(TEXT('/'),SlashIdx)&&SlashIdx>1)?InPath.Left(SlashIdx):InPath; }

void UVRMSpringBonesPostImportPipeline::RegisterDeferredABP(const FString& InSkeletonSearchRoot, const FString& InPackagePath, UVRMSpringBoneData* InSpringData, bool bInWantsAssign)
{
    UnregisterDeferredABP(); DeferredSkeletonSearchRoot=InSkeletonSearchRoot; DeferredAltSkeletonSearchRoot=GetParentPackagePath(InSkeletonSearchRoot); DeferredPackagePath=InPackagePath; DeferredSpringDataAsset=InSpringData; bDeferredWantsAssign=bInWantsAssign; bDeferredCompleted=false; FAssetRegistryModule& ARM=FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry"); DeferredHandle=ARM.Get().OnAssetAdded().AddUObject(this,&UVRMSpringBonesPostImportPipeline::OnAssetAddedForDeferredABP); }

void UVRMSpringBonesPostImportPipeline::UnregisterDeferredABP()
{
    if(DeferredHandle.IsValid()) { if(FModuleManager::Get().IsModuleLoaded("AssetRegistry")){ FAssetRegistryModule& ARM=FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry"); ARM.Get().OnAssetAdded().Remove(DeferredHandle);} DeferredHandle.Reset(); }
}

void UVRMSpringBonesPostImportPipeline::OnAssetAddedForDeferredABP(const FAssetData& AssetData)
{
    if(bDeferredCompleted||!AssetData.IsValid()) return; const FName ClassName=AssetData.AssetClassPath.GetAssetName(); const bool bIsSkeletalMesh=(ClassName==USkeletalMesh::StaticClass()->GetFName()); const bool bIsSkeleton=(ClassName==USkeleton::StaticClass()->GetFName()); if(!bIsSkeletalMesh&&!bIsSkeleton) return; const FString PkgPath=AssetData.PackagePath.ToString(); if(!PkgPath.StartsWith(DeferredSkeletonSearchRoot)&&!PkgPath.StartsWith(DeferredAltSkeletonSearchRoot)) return; USkeletalMesh* SkelMesh=nullptr; USkeleton* Skeleton=nullptr; bool bFound=FindImportedSkeletalAssets(DeferredSkeletonSearchRoot,SkelMesh,Skeleton)&&(SkelMesh||Skeleton); if(!bFound) bFound=FindImportedSkeletalAssets(DeferredAltSkeletonSearchRoot,SkelMesh,Skeleton)&&(SkelMesh||Skeleton); if(!bFound) return; UObject* DuplicatedABP=DuplicateTemplateAnimBlueprint(DeferredPackagePath/AnimationSubFolder,TEXT("ABP_VRMSpringBones"),Skeleton?Skeleton:(SkelMesh?SkelMesh->GetSkeleton():nullptr)); if(!DuplicatedABP){ UnregisterDeferredABP(); bDeferredCompleted=true; return; } if(UVRMSpringBoneData* SpringData=DeferredSpringDataAsset.Get()){ SetSpringConfigOnAnimBlueprint(DuplicatedABP,SpringData);} if(bDeferredWantsAssign&&SkelMesh){ AssignPostProcessABPToMesh(SkelMesh,DuplicatedABP); if(UPackage* MeshPkg=SkelMesh->GetOutermost()){ const FString MeshPkgFilename=FPackageName::LongPackageNameToFilename(MeshPkg->GetName(),FPackageName::GetAssetPackageExtension()); FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags=RF_Public|RF_Standalone; SaveArgs.SaveFlags=SAVE_NoError; UPackage::SavePackage(MeshPkg,SkelMesh,*MeshPkgFilename,SaveArgs);} } if(UObject* Outer=DuplicatedABP->GetOutermost()) if(UPackage* Pkg=Cast<UPackage>(Outer)){ const FString FN=FPackageName::LongPackageNameToFilename(Pkg->GetName(),FPackageName::GetAssetPackageExtension()); FSavePackageArgs SaveArgs; SaveArgs.TopLevelFlags=RF_Public|RF_Standalone; SaveArgs.SaveFlags=SAVE_NoError; UPackage::SavePackage(Pkg,nullptr,*FN,SaveArgs);} UnregisterDeferredABP(); bDeferredCompleted=true; }