#include "CoreMinimal.h"
// VRMSpringBonesPostImportPipeline.cpp
#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMSpringBoneData.h"

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

// NEW: use shared parser + log category
#include "VRMSpringBonesParser.h"
#include "VRMInterchangeLog.h"

// Project settings for defaults
#include "VRMInterchangeSettings.h"

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

// For skeleton/mesh
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
    const bool bWantsOverwrite = (bOverwriteExisting || (Settings && Settings->bOverwriteExistingSpringAssets));

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

    const FString SkeletonSearchRoot = PackagePath; // before SubFolder

    if (!DataAssetName.IsEmpty())
    {
        SpringAssetName = DataAssetName;
    }

    FString SpringDataFolder = PackagePath;
    if (!SubFolder.IsEmpty())
    {
        SpringDataFolder = SpringDataFolder / SubFolder;
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

    // Create / update SpringBonesData if desired
    UVRMSpringBoneData* SpringDataAsset = nullptr;
    if (bWantsSpringData)
    {
        SpringDataAsset = FindObject<UVRMSpringBoneData>(SpringPackage, *SpringAssetName);
        bool bIsNewSpring = false;
        if (!SpringDataAsset)
        {
            if (FindObject<UObject>(SpringPackage, *SpringAssetName))
            {
                UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Conflicting object '%s' exists in '%s'."), *SpringAssetName, *LongSpringPackageName);
                return;
            }
            SpringDataAsset = NewObject<UVRMSpringBoneData>(SpringPackage, *SpringAssetName, RF_Public | RF_Standalone);
            bIsNewSpring = true;
        }

        if (!ParseAndFillDataAssetFromFile(Filename, SpringDataAsset))
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No spring data found in '%s'."), *Filename);
            // Continue — we can still generate ABP scaffold if requested, even without spring data.
        }
        else
        {
            // Resolve bone names from node indices where possible
            int32 ResolvedC = 0, ResolvedJ = 0, ResolvedCenters = 0;
            ResolveBoneNamesFromFile(Filename, SpringDataAsset->SpringConfig, ResolvedC, ResolvedJ, ResolvedCenters);

            // Validate against imported skeleton and log any missing names
            ValidateBoneNamesAgainstSkeleton(SkeletonSearchRoot, SpringDataAsset->SpringConfig);

            // Provenance
            SpringDataAsset->SourceFilename = Filename;
            SpringDataAsset->SourceHash = LexToString(FMD5Hash::HashFile(*Filename));

            // Summary log
            const TCHAR* SpecStr = TEXT("None");
            switch (SpringDataAsset->SpringConfig.Spec)
            {
            case EVRMSpringSpec::VRM0: SpecStr = TEXT("VRM0"); break;
            case EVRMSpringSpec::VRM1: SpecStr = TEXT("VRM1"); break;
            default: break;
            }
            UE_LOG(LogVRMSpring, Log, TEXT("[VRMInterchange] Spring pipeline: Parsed %s springs: Springs=%d Joints=%d Colliders=%d Groups=%d  Resolved: Joints=%d Colliders=%d Centers=%d"),
                SpecStr,
                SpringDataAsset->SpringConfig.Springs.Num(),
                SpringDataAsset->SpringConfig.Joints.Num(),
                SpringDataAsset->SpringConfig.Colliders.Num(),
                SpringDataAsset->SpringConfig.ColliderGroups.Num(),
                ResolvedJ, ResolvedC, ResolvedCenters);

            if (bIsNewSpring)
            {
                FAssetRegistryModule::AssetCreated(SpringDataAsset);
            }
        }

        SpringDataAsset->MarkPackageDirty();
        SpringPackage->SetDirtyFlag(true);

        // Save the package to disk to make asset immediately available
        {
            const FString PackageFilename = FPackageName::LongPackageNameToFilename(SpringPackage->GetName(), FPackageName::GetAssetPackageExtension());
            FSavePackageArgs SaveArgs;
            SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
            SaveArgs.SaveFlags = SAVE_NoError;
            UPackage::SavePackage(SpringPackage, SpringDataAsset, *PackageFilename, SaveArgs);
        }
    }

    // Phase 3: Generate and assign Post-Process Anim Blueprint
    const bool bWantsABP = (bGeneratePostProcessAnimBP || (Settings && Settings->bGeneratePostProcessAnimBP));
    const bool bWantsAssign = (bAssignPostProcessABP || (Settings && Settings->bAssignPostProcessABP));

    if (bWantsABP)
    {
        USkeletalMesh* SkelMesh = nullptr; USkeleton* Skeleton = nullptr;
        if (!FindImportedSkeletalAssets(SkeletonSearchRoot, SkelMesh, Skeleton) || (!SkelMesh && !Skeleton))
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No skeletal assets found under '%s' for ABP scaffolding."), *SkeletonSearchRoot);
        }
        else
        {
            const FString AnimFolder = PackagePath / AnimationSubFolder;
            const FString BaseABPName = TEXT("ABP_VRMSpringBones");

            UObject* DuplicatedABP = DuplicateTemplateAnimBlueprint(AnimFolder, BaseABPName, Skeleton ? Skeleton : (SkelMesh ? SkelMesh->GetSkeleton() : nullptr));
            if (DuplicatedABP)
            {
                if (SpringDataAsset)
                {
                    SetSpringConfigOnAnimBlueprint(DuplicatedABP, SpringDataAsset);
                }

                if (bWantsAssign && SkelMesh)
                {
                    AssignPostProcessABPToMesh(SkelMesh, DuplicatedABP);
                }

                // Save duplicated ABP package too for consistency
                if (UObject* DuplicatedOuter = DuplicatedABP->GetOutermost())
                {
                    UPackage* ABPPackage = Cast<UPackage>(DuplicatedOuter);
                    if (ABPPackage)
                    {
                        const FString ABPPackageFilename = FPackageName::LongPackageNameToFilename(ABPPackage->GetName(), FPackageName::GetAssetPackageExtension());
                        FSavePackageArgs SaveArgs;
                        SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
                        SaveArgs.SaveFlags = SAVE_NoError;
                        UPackage::SavePackage(ABPPackage, nullptr, *ABPPackageFilename, SaveArgs);
                    }
                }
            }
        }
    }

#endif
}

#if WITH_EDITOR
// Use shared Phase 1 parser
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
    if (!ContentBasePath.IsEmpty())
    {
        OutPackagePath = (ContentBasePath / BaseName);
    }
    else
    {
        OutPackagePath = FString::Printf(TEXT("/Game/%s"), *BaseName);
    }
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
    struct FCgltfScopedLocal { cgltf_data* D; ~FCgltfScopedLocal(){ if (D) cgltf_free(D); } } Scoped{ Data };

    const int32 NodesCount = static_cast<int32>(Data->nodes_count);
    auto GetNodeName = [&](int32 NodeIndex) -> FName
    {
        if (NodeIndex < 0 || NodeIndex >= NodesCount) return NAME_None;
        const cgltf_node* N = &Data->nodes[NodeIndex];
        if (N && N->name && N->name[0] != '\0')
        { return FName(UTF8_TO_TCHAR(N->name)); }
        return NAME_None;
    };

    for (FVRMSpringCollider& C : InOut.Colliders)
    { if (C.BoneName.IsNone() && C.NodeIndex != INDEX_NONE) { const FName Nm = GetNodeName(C.NodeIndex); if (!Nm.IsNone()) { C.BoneName = Nm; ++OutResolvedColliders; } } }
    for (FVRMSpringJoint& J : InOut.Joints)
    { if (J.BoneName.IsNone() && J.NodeIndex != INDEX_NONE) { const FName Nm = GetNodeName(J.NodeIndex); if (!Nm.IsNone()) { J.BoneName = Nm; ++OutResolvedJoints; } } }
    for (FVRMSpring& S : InOut.Springs)
    { if (S.CenterBoneName.IsNone() && S.CenterNodeIndex != INDEX_NONE) { const FName Nm = GetNodeName(S.CenterNodeIndex); if (!Nm.IsNone()) { S.CenterBoneName = Nm; ++OutResolvedCenters; } } }

    return (OutResolvedColliders + OutResolvedJoints + OutResolvedCenters) > 0;
#else
    UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: cgltf.h not available; skipping bone name resolution."));
    OutResolvedColliders = OutResolvedJoints = OutResolvedCenters = 0; return false;
#endif
}

void UVRMSpringBonesPostImportPipeline::ValidateBoneNamesAgainstSkeleton(const FString& SearchRootPackagePath, const FVRMSpringConfig& Config) const
{
    if (SearchRootPackagePath.IsEmpty())
    {
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No ContentBasePath; skipping skeleton validation."));
        return;
    }

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    // Try USkeleton first, then fallback to USkeletalMesh
    FARFilter Filter; Filter.bRecursivePaths = true; Filter.PackagePaths.Add(*SearchRootPackagePath); Filter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName());
    TArray<FAssetData> Found; ARM.Get().GetAssets(Filter, Found);
    USkeleton* Skeleton = nullptr;
    if (Found.Num() > 0) { Skeleton = Cast<USkeleton>(Found[0].GetAsset()); }
    if (!Skeleton)
    {
        FARFilter MeshFilter; MeshFilter.bRecursivePaths = true; MeshFilter.PackagePaths.Add(*SearchRootPackagePath); MeshFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
        TArray<FAssetData> Meshes; ARM.Get().GetAssets(MeshFilter, Meshes);
        if (Meshes.Num() > 0) { if (USkeletalMesh* SM = Cast<USkeletalMesh>(Meshes[0].GetAsset())) { Skeleton = SM->GetSkeleton(); } }
    }

    if (!Skeleton) { return; }

    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    TSet<FName> ValidBones; for (int32 i=0;i<RefSkel.GetNum();++i) { ValidBones.Add(RefSkel.GetBoneName(i)); }

    auto CheckNames = [&](const TArray<FName>& Names, const TCHAR* What)
    {
        if (Names.Num() > 0) { UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: %d %s not found on skeleton '%s'"), Names.Num(), What, *Skeleton->GetPathName()); }
    };

    TArray<FName> MissingColliders, MissingJoints, MissingCenters;
    for (const FVRMSpringCollider& C : Config.Colliders) { if (!C.BoneName.IsNone() && !ValidBones.Contains(C.BoneName)) MissingColliders.AddUnique(C.BoneName); }
    for (const FVRMSpringJoint& J : Config.Joints) { if (!J.BoneName.IsNone() && !ValidBones.Contains(J.BoneName)) MissingJoints.AddUnique(J.BoneName); }
    for (const FVRMSpring& S : Config.Springs) { if (!S.CenterBoneName.IsNone() && !ValidBones.Contains(S.CenterBoneName)) MissingCenters.AddUnique(S.CenterBoneName); }

    CheckNames(MissingColliders, TEXT("collider BoneName(s)"));
    CheckNames(MissingJoints, TEXT("joint BoneName(s)"));
    CheckNames(MissingCenters, TEXT("center BoneName(s)"));
}

bool UVRMSpringBonesPostImportPipeline::FindImportedSkeletalAssets(const FString& SearchRootPackagePath, USkeletalMesh*& OutSkeletalMesh, USkeleton*& OutSkeleton) const
{
    OutSkeletalMesh = nullptr; OutSkeleton = nullptr;
    if (SearchRootPackagePath.IsEmpty()) return false;

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    // Prefer USkeletalMesh
    FARFilter MeshFilter; MeshFilter.bRecursivePaths = true; MeshFilter.PackagePaths.Add(*SearchRootPackagePath); MeshFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
    TArray<FAssetData> Meshes; ARM.Get().GetAssets(MeshFilter, Meshes);
    if (Meshes.Num() > 0)
    {
        OutSkeletalMesh = Cast<USkeletalMesh>(Meshes[0].GetAsset());
        if (OutSkeletalMesh) { OutSkeleton = OutSkeletalMesh->GetSkeleton(); }
    }

    // If still no skeleton, search directly
    if (!OutSkeleton)
    {
        FARFilter SkelFilter; SkelFilter.bRecursivePaths = true; SkelFilter.PackagePaths.Add(*SearchRootPackagePath); SkelFilter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName());
        TArray<FAssetData> Skels; ARM.Get().GetAssets(SkelFilter, Skels);
        if (Skels.Num() > 0) { OutSkeleton = Cast<USkeleton>(Skels[0].GetAsset()); }
    }

    return (OutSkeletalMesh != nullptr) || (OutSkeleton != nullptr);
}

UObject* UVRMSpringBonesPostImportPipeline::DuplicateTemplateAnimBlueprint(const FString& TargetPackagePath, const FString& BaseName, USkeleton* TargetSkeleton) const
{
    if (!TargetSkeleton) return nullptr;

    // Template lives in plugin content
    const TCHAR* TemplatePath = TEXT("/VRMInterchange/Animation/ABP_VRMSpringBones_Template.ABP_VRMSpringBones_Template");
    UAnimBlueprint* TemplateABP = Cast<UAnimBlueprint>(StaticLoadObject(UAnimBlueprint::StaticClass(), nullptr, TemplatePath));
    if (!TemplateABP)
    {
        UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Could not find template AnimBlueprint at '%s'."), TemplatePath);
        return nullptr;
    }

    FString NewAssetPath = TargetPackagePath / BaseName;

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    FString UniquePath, UniqueName; AssetToolsModule.Get().CreateUniqueAssetName(NewAssetPath, TEXT(""), UniquePath, UniqueName);

    const FString LongPackage = UniquePath.StartsWith(TEXT("/")) ? UniquePath : TEXT("/") + UniquePath;
    UPackage* Pkg = CreatePackage(*LongPackage);
    if (!Pkg) return nullptr;

    UObject* Duplicated = StaticDuplicateObject(TemplateABP, Pkg, *UniqueName);
    if (!Duplicated)
    {
        UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Failed to duplicate template ABP."));
        return nullptr;
    }

    // Notify asset registry
    FAssetRegistryModule::AssetCreated(Duplicated);

    // Retarget to skeleton
    if (UAnimBlueprint* NewABP = Cast<UAnimBlueprint>(Duplicated))
    {
        NewABP->TargetSkeleton = TargetSkeleton; // Set before compilation
        FKismetEditorUtilities::CompileBlueprint(NewABP);
    }

    return Duplicated;
}

bool UVRMSpringBonesPostImportPipeline::SetSpringConfigOnAnimBlueprint(UObject* AnimBlueprintObj, UVRMSpringBoneData* SpringData) const
{
    if (!AnimBlueprintObj || !SpringData) return false;
    if (UAnimBlueprint* ABP = Cast<UAnimBlueprint>(AnimBlueprintObj))
    {
        // Ensure the blueprint is compiled first
        FKismetEditorUtilities::CompileBlueprint(ABP);

        // Try to set SpringConfig if it's a hard UObject property (not soft)
        UClass* BPClass = ABP->GeneratedClass;
        if (BPClass)
        {
            for (TFieldIterator<FProperty> PropIt(BPClass); PropIt; ++PropIt)
            {
                FProperty* Prop = *PropIt;
                if (Prop && Prop->GetName() == TEXT("SpringConfig"))
                {
                    // Only set if it's a hard UObject property (not soft)
                    if (FObjectProperty* ObjProp = CastField<FObjectProperty>(Prop))
                    {
                        if (!ObjProp->IsA<FSoftObjectProperty>())
                        {
                            void* ObjAddr = ObjProp->ContainerPtrToValuePtr<void>(ABP);
                            ObjProp->SetObjectPropertyValue(ObjAddr, SpringData);
                            return true;
                        }
                    }
                }
            }
        }
        return true; // Compiled, but property not set
    }
    return false;
}



bool UVRMSpringBonesPostImportPipeline::AssignPostProcessABPToMesh(USkeletalMesh* SkelMesh, UObject* AnimBlueprintObj) const
{
    if (!SkelMesh || !AnimBlueprintObj) return false;
    UClass* BPClass = nullptr;
    if (UAnimBlueprint* ABP = Cast<UAnimBlueprint>(AnimBlueprintObj))
    {
        FKismetEditorUtilities::CompileBlueprint(ABP);
        BPClass = ABP->GeneratedClass;
    }
    else
    {
        BPClass = Cast<UClass>(AnimBlueprintObj);
    }
    if (!BPClass) return false;

    // Assign as post-process AnimBP
	SkelMesh->SetPostProcessAnimBlueprint(BPClass);
    SkelMesh->MarkPackageDirty();
    return true;
}
#endif