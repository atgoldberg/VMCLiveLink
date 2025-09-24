// VRMSpringBonesPostImportPipeline.cpp
// Fixed: Removed immediate package saving during import to prevent interference with main VRM import process
// The pipeline now defers saving to Unreal's asset management system to avoid race conditions and resource conflicts
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

// Removed explicit SavePackage include to avoid saving during pipeline execution

// NEW: use shared parser + log category
#include "VRMSpringBonesParser.h"
#include "VRMInterchangeLog.h"

// cgltf support (needed by other TUs in the module). Provide implementation here when available.
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

// For skeleton validation
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"

void UVRMSpringBonesPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
#if WITH_EDITOR
    // Early exit if spring bone generation is disabled or if we don't have the required inputs
    if (!bGenerateSpringBoneData || !BaseNodeContainer)
    {
        return;
    }

    const UInterchangeSourceData* Source = nullptr;
    if (SourceDatas.Num() > 0)
    {
        for (const UInterchangeSourceData* SD : SourceDatas)
        {
            if (SD)
            {
                Source = SD;
                break;
            }
        }
    }

    if (!Source)
    {
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No SourceData."));
        return;
    }

    const FString Filename = Source->GetFilename();

    // Create or reuse a package under the imported asset path (prefer ContentBasePath)
    FString PackagePath, AssetName;
    MakeTargetPathAndName(Filename, ContentBasePath, PackagePath, AssetName);

    UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: BasePath='%s' => CharacterPath='%s'"), *ContentBasePath, *PackagePath);

    // Keep a root search path for skeleton validation (without SubFolder)
    const FString SkeletonSearchRoot = PackagePath;

    if (!DataAssetName.IsEmpty())
    {
        AssetName = DataAssetName;
    }

    if (!SubFolder.IsEmpty())
    {
        PackagePath = PackagePath / SubFolder;
    }

    UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: FinalPackagePath='%s'"), *PackagePath);

    // Ensure unique or overwrite as requested
    FString FinalPackageName = PackagePath / AssetName;
    if (!bOverwriteExisting)
    {
        FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
        AssetToolsModule.Get().CreateUniqueAssetName(FinalPackageName, TEXT(""), FinalPackageName, AssetName);
    }

    FString LongPackageName = FinalPackageName;
    if (!LongPackageName.StartsWith(TEXT("/")))
    {
        LongPackageName = TEXT("/") + LongPackageName;
    }

    UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: FinalAssetPath='%s'"), *LongPackageName);

    // Create the package if needed
    UPackage* Package = CreatePackage(*LongPackageName);

    if (!Package)
    {
        UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Failed to create/find package '%s'."), *LongPackageName);
        return;
    }

    // Find or create the asset (be more defensive about existing assets)
    UVRMSpringBoneData* Data = FindObject<UVRMSpringBoneData>(Package, *AssetName);
    if (!Data)
    {
        // Double-check that we won't be creating a conflicting asset
        if (Package->FindExportObject(UVRMSpringBoneData::StaticClass(), FName(*AssetName)))
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Asset '%s' already exists with different type in package '%s'."), *AssetName, *LongPackageName);
            return;
        }
        
        Data = NewObject<UVRMSpringBoneData>(Package, *AssetName, RF_Public | RF_Standalone);
    }
    if (!Data)
    {
        UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Failed to allocate UVRMSpringBoneData."));
        return;
    }

    // Fill asset by parsing from file using the shared parser (removes duplicate JSON extraction)
    if (!ParseAndFillDataAssetFromFile(Filename, Data))
    {
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No spring data found in '%s'."), *Filename);
        return;
    }

    // Resolve BoneName / CenterBoneName by re-parsing the source glTF/VRM with cgltf
    int32 ResolvedColliders = 0, ResolvedJoints = 0, ResolvedCenters = 0;
    if (!ResolveBoneNamesFromFile(Filename, Data->SpringConfig, ResolvedColliders, ResolvedJoints, ResolvedCenters))
    {
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: Could not resolve bone names from '%s'."), *Filename);
    }

    // Validate resolved names against a USkeleton found under SkeletonSearchRoot
    ValidateBoneNamesAgainstSkeleton(SkeletonSearchRoot, Data->SpringConfig);

    // Summary logs (spec + counts)
    {
        auto SpecToString = [](EVRMSpringSpec S) -> const TCHAR*
        {
            switch (S)
            {
                case EVRMSpringSpec::VRM0: return TEXT("VRM0");
                case EVRMSpringSpec::VRM1: return TEXT("VRM1");
                default: return TEXT("None");
            }
        };

        const FVRMSpringConfig& Cfg = Data->SpringConfig;
        UE_LOG(
            LogVRMSpring, Log,
            TEXT("[VRMInterchange] Spring pipeline: Detected spec=%s, Springs=%d, Joints=%d, Colliders=%d, ColliderGroups=%d"),
            SpecToString(Cfg.Spec),
            Cfg.Springs.Num(),
            Cfg.Joints.Num(),
            Cfg.Colliders.Num(),
            Cfg.ColliderGroups.Num()
        );

        // Also log resolution counts
        int32 SpringsWithCenter = 0;
        for (const auto& S : Cfg.Springs)
        {
            if (S.CenterNodeIndex != INDEX_NONE) { SpringsWithCenter++; }
        }
        UE_LOG(
            LogVRMSpring, Log,
            TEXT("[VRMInterchange] Spring pipeline: Resolved BoneNames — Colliders %d/%d, Joints %d/%d, Centers %d/%d"),
            ResolvedColliders, Cfg.Colliders.Num(),
            ResolvedJoints,   Cfg.Joints.Num(),
            ResolvedCenters,  SpringsWithCenter
        );
    }

    // Save source info
    Data->SourceFilename = Filename;
    {
        const TOptional<FMD5Hash> MaybeHash = Source->GetFileContentHash();
        if (MaybeHash.IsSet() && MaybeHash->IsValid())
        {
            Data->SourceHash = LexToString(MaybeHash.GetValue());
        }
        else
        {
            Data->SourceHash.Empty();
        }
    }


    // Register the asset but do NOT save packages here; saving during pipeline execution can
    // interfere with Interchange factory import (locks packages and can break asset creation).

    Data->MarkPackageDirty();
    Package->SetDirtyFlag(true);
    UE_LOG(LogVRMSpring, Log, TEXT("[VRMInterchange] Spring pipeline: Authored '%s' (will be saved by the import system)"), *Data->GetPathName());

#endif
}

#if WITH_EDITOR
// Use shared Phase 1 parser and store normalized config (from file)
bool UVRMSpringBonesPostImportPipeline::ParseAndFillDataAssetFromFile(const FString& Filename, UVRMSpringBoneData* Dest) const
{
    if (!Dest) return false;

    FVRMSpringConfig Config;
    FString Err;
    if (!VRM::ParseSpringBonesFromFile(Filename, Config, Err))
    {
        return false;
    }

    Dest->SpringConfig = MoveTemp(Config);
    return Dest->SpringConfig.IsValid();
}

FString UVRMSpringBonesPostImportPipeline::MakeTargetPathAndName(const FString& SourceFilename, const FString& ContentBasePath, FString& OutPackagePath, FString& OutAssetName) const
{
    // Character folder name derived from the source file base name
    const FString BaseName = FPaths::GetBaseFilename(SourceFilename);

    if (!ContentBasePath.IsEmpty())
    {
        OutPackagePath = ContentBasePath;
        // Remove any trailing slash to analyze the last segment correctly
        while (OutPackagePath.Len() > 1 && (OutPackagePath.EndsWith(TEXT("/")) || OutPackagePath.EndsWith(TEXT("\\"))))
        {
            OutPackagePath.LeftChopInline(1);
        }
        const FString LastSegment = FPaths::GetCleanFilename(OutPackagePath);
        if (!LastSegment.Equals(BaseName, ESearchCase::IgnoreCase))
        {
            OutPackagePath = OutPackagePath / BaseName; // ensure /.../<VRMCharacterName>
        }
    }
    else
    {
        // Fallback: derive from source file name
        OutPackagePath = FString::Printf(TEXT("/Game/%s"), *BaseName);
    }

    // Name defaults
    OutAssetName = TEXT("SpringBonesData");
    return OutPackagePath / OutAssetName;
}

// Resolve BoneName and CenterBoneName using cgltf node names
bool UVRMSpringBonesPostImportPipeline::ResolveBoneNamesFromFile(const FString& Filename, FVRMSpringConfig& InOut, int32& OutResolvedColliders, int32& OutResolvedJoints, int32& OutResolvedCenters) const
{
#if VRM_HAS_CGLTF
    OutResolvedColliders = 0;
    OutResolvedJoints = 0;
    OutResolvedCenters = 0;

    if (!InOut.IsValid())
    {
        return false;
    }

    FTCHARToUTF8 PathUtf8(*Filename);
    cgltf_options Options = {};
    cgltf_data* Data = nullptr;
    const cgltf_result Res = cgltf_parse_file(&Options, PathUtf8.Get(), &Data);
    if (Res != cgltf_result_success || !Data)
    {
        return false;
    }

    struct FCgltfScopedLocal { cgltf_data* D; ~FCgltfScopedLocal(){ if (D) cgltf_free(D); } } Scoped{ Data };

    const int32 NodesCount = static_cast<int32>(Data->nodes_count);
    auto GetNodeName = [&](int32 NodeIndex) -> FName
    {
        if (NodeIndex < 0 || NodeIndex >= NodesCount) return NAME_None;
        const cgltf_node* N = &Data->nodes[NodeIndex];
        if (N && N->name && N->name[0] != '\0')
        {
            const FString NameStr = UTF8_TO_TCHAR(N->name);
            return FName(*NameStr);
        }
        return NAME_None;
    };

    // Colliders
    for (FVRMSpringCollider& C : InOut.Colliders)
    {
        if (C.BoneName.IsNone() && C.NodeIndex != INDEX_NONE)
        {
            const FName Name = GetNodeName(C.NodeIndex);
            if (!Name.IsNone())
            {
                C.BoneName = Name;
                ++OutResolvedColliders;
            }
        }
    }

    // Joints
    for (FVRMSpringJoint& J : InOut.Joints)
    {
        if (J.BoneName.IsNone() && J.NodeIndex != INDEX_NONE)
        {
            const FName Name = GetNodeName(J.NodeIndex);
            if (!Name.IsNone())
            {
                J.BoneName = Name;
                ++OutResolvedJoints;
            }
        }
    }

    // Springs centers
    for (FVRMSpring& S : InOut.Springs)
    {
        if (S.CenterBoneName.IsNone() && S.CenterNodeIndex != INDEX_NONE)
        {
            const FName Name = GetNodeName(S.CenterNodeIndex);
            if (!Name.IsNone())
            {
                S.CenterBoneName = Name;
                ++OutResolvedCenters;
            }
        }
    }

    return (OutResolvedColliders + OutResolvedJoints + OutResolvedCenters) > 0;
#else
    UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: cgltf.h not available; skipping bone name resolution."));
    OutResolvedColliders = OutResolvedJoints = OutResolvedCenters = 0;
    return false;
#endif
}

// Validate BoneNames against a USkeleton under the imported asset's folder
void UVRMSpringBonesPostImportPipeline::ValidateBoneNamesAgainstSkeleton(const FString& SearchRootPackagePath, const FVRMSpringConfig& Config) const
{
    if (SearchRootPackagePath.IsEmpty())
    {
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No ContentBasePath; skipping skeleton validation."));
        return;
    }

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

    // Try to find a USkeleton first
    FARFilter Filter;
    Filter.bRecursivePaths = true;
    Filter.PackagePaths.Add(*SearchRootPackagePath);
    Filter.ClassPaths.Add(USkeleton::StaticClass()->GetClassPathName());

    TArray<FAssetData> Found;
    ARM.Get().GetAssets(Filter, Found);

    USkeleton* Skeleton = nullptr;

    if (Found.Num() > 0)
    {
        // Load the first skeleton found
        Skeleton = Cast<USkeleton>(Found[0].GetAsset());
    }
    else
    {
        // Fallback: find a USkeletalMesh and get its skeleton
        FARFilter MeshFilter;
        MeshFilter.bRecursivePaths = true;
        MeshFilter.PackagePaths.Add(*SearchRootPackagePath);
        MeshFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());

        TArray<FAssetData> Meshes;
        ARM.Get().GetAssets(MeshFilter, Meshes);
        if (Meshes.Num() > 0)
        {
            if (USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Meshes[0].GetAsset()))
            {
                Skeleton = SkelMesh->GetSkeleton();
            }
        }
    }

    if (!Skeleton)
    {
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No USkeleton found under '%s' for validation."), *SearchRootPackagePath);
        return;
    }

    // Build a set of valid bone names
    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    TSet<FName> ValidBones;
    ValidBones.Reserve(RefSkel.GetNum());
    for (int32 i = 0; i < RefSkel.GetNum(); ++i)
    {
        ValidBones.Add(RefSkel.GetBoneName(i));
    }

    // Collect mismatches
    TArray<FName> MissingColliders;
    TArray<FName> MissingJoints;
    TArray<FName> MissingCenters;

    for (const FVRMSpringCollider& C : Config.Colliders)
    {
        if (!C.BoneName.IsNone() && !ValidBones.Contains(C.BoneName))
        {
            MissingColliders.AddUnique(C.BoneName);
        }
    }
    for (const FVRMSpringJoint& J : Config.Joints)
    {
        if (!J.BoneName.IsNone() && !ValidBones.Contains(J.BoneName))
        {
            MissingJoints.AddUnique(J.BoneName);
        }
    }
    for (const FVRMSpring& S : Config.Springs)
    {
        if (!S.CenterBoneName.IsNone() && !ValidBones.Contains(S.CenterBoneName))
        {
            MissingCenters.AddUnique(S.CenterBoneName);
        }
    }

    auto JoinNames = [](const TArray<FName>& Names)->FString
    {
        FString Out;
        const int32 MaxList = 12; // cap output
        const int32 Count = Names.Num();
        for (int32 i = 0; i < Count && i < MaxList; ++i)
        {
            if (i) Out += TEXT(", ");
            Out += Names[i].ToString();
        }
        if (Count > MaxList)
        {
            Out += FString::Printf(TEXT(", +%d more"), Count - MaxList);
        }
        return Out;
    };

    if (MissingColliders.Num() == 0 && MissingJoints.Num() == 0 && MissingCenters.Num() == 0)
    {
        UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: All resolved BoneNames validated against '%s'."), *Skeleton->GetPathName());
    }
    else
    {
        if (MissingColliders.Num() > 0)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: %d collider BoneName(s) not found on skeleton '%s': %s"),
                MissingColliders.Num(), *Skeleton->GetPathName(), *JoinNames(MissingColliders));
        }
        if (MissingJoints.Num() > 0)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: %d joint BoneName(s) not found on skeleton '%s': %s"),
                MissingJoints.Num(), *Skeleton->GetPathName(), *JoinNames(MissingJoints));
        }
        if (MissingCenters.Num() > 0)
        {
            UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: %d center BoneName(s) not found on skeleton '%s': %s"),
                MissingCenters.Num(), *Skeleton->GetPathName(), *JoinNames(MissingCenters));
        }
    }
}
#endif