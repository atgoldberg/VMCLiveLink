// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// VRMSpringBonesPostImportPipeline
// - Defers any on-disk mutations until after the user confirms the import dialog.
// - Creates assets (Spring data + optional post-process ABP), marks packages dirty,
//   and does NOT save during import. Let the editor Save/Source Control flow handle persistence.
// - Supports unattended/CI mode if you later want an opt-in "save immediately" gate.

#include "VRMSpringBonesPostImportPipeline.h"
#include "VRMSpringBoneData.h"                // runtime asset
#include "VRMInterchangeEditorModule.h"       // notifications
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "Misc/SecureHash.h"
#include "VRMSpringBonesParser.h"
#include "VRMInterchangeLog.h"
#include "VRMInterchangeSettings.h"

#if WITH_EDITOR
#include "UnrealEdGlobals.h"
#include "Subsystems/ImportSubsystem.h"
#endif

/// cgltf
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
            bOverwriteExistingPostProcessABP = Settings->bOverwriteExistingPostProcessABP;
            bReusePostProcessABPOnReimport  = Settings->bReusePostProcessABPOnReimport;
            // bConvertToUEUnits remains default-true unless a project setting is added later.
        }
    }
}
#endif

// ExecutePipeline
// - Parse VRM spring data into a transient asset
// - Stage all decisions (paths, names, flags)
// - Register a post-import handler to materialize assets after the import dialog is accepted
void UVRMSpringBonesPostImportPipeline::ExecutePipeline(UInterchangeBaseNodeContainer* BaseNodeContainer, const TArray<UInterchangeSourceData*>& SourceDatas, const FString& ContentBasePath)
{
#if WITH_EDITOR
    const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>();
    const bool bWantsSpringData    = (bGenerateSpringBoneData || (Settings && Settings->bGenerateSpringBoneData));
    const bool bWantsOverwrite     = (bOverwriteExisting || (Settings && Settings->bOverwriteExistingSpringAssets));
    const bool bWantsABPOverwrite  = (bOverwriteExistingPostProcessABP || (Settings && Settings->bOverwriteExistingPostProcessABP));
    const bool bWantsReuseABP      = (bReusePostProcessABPOnReimport || (Settings && Settings->bReusePostProcessABPOnReimport));

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

    // Compute character base package path only; defer asset naming to post-import.
    const FString BaseName = FPaths::GetBaseFilename(Filename);
    const FString PackagePath = !ContentBasePath.IsEmpty()
        ? (ContentBasePath / BaseName)
        : FString::Printf(TEXT("/Game/%s"), *BaseName);

    const FString SkeletonSearchRoot = PackagePath;
    const FString ParentSearchRoot   = GetParentPackagePath(SkeletonSearchRoot);

    // Early tombstone check
    FString SourceHash;
    if (FPaths::FileExists(Filename))
    {
        SourceHash = LexToString(FMD5Hash::HashFile(*Filename));
    }

    // Prepare transient spring data (no assets created on disk)
    UVRMSpringBoneData* TransientSpringData = nullptr;
    if (bWantsSpringData)
    {
        TransientSpringData = NewObject<UVRMSpringBoneData>(GetTransientPackage(), NAME_None);
        if (!ParseAndFillDataAssetFromFile(Filename, TransientSpringData))
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMInterchange] Spring pipeline: No spring data found in '%s'."), *Filename);
            TransientSpringData = nullptr;
        }
        else
        {
            int32 ResolvedC=0, ResolvedJ=0, ResolvedCenters=0;
            ResolveBoneNamesFromFile(Filename, TransientSpringData->SpringConfig, ResolvedC, ResolvedJ, ResolvedCenters);

            // Convert SpringData units to UE (cm) if requested
            if (bConvertToUEUnits)
            {
                ConvertSpringConfigToUEUnits(TransientSpringData->SpringConfig);
            }

            ValidateBoneNamesAgainstSkeleton(SkeletonSearchRoot, TransientSpringData->SpringConfig);
            TransientSpringData->SourceFilename = Filename;
            if (!SourceHash.IsEmpty())
            {
                TransientSpringData->SourceHash = SourceHash;
            }
        }
    }

    const bool bWantsABP  = (bGeneratePostProcessAnimBP || (Settings && Settings->bGeneratePostProcessAnimBP));
    const bool bWantsAssign = (bAssignPostProcessABP || (Settings && Settings->bAssignPostProcessABP));

    // Stage state for the post-import commit
    DeferredSkeletonSearchRoot    = SkeletonSearchRoot;
    DeferredAltSkeletonSearchRoot = ParentSearchRoot;
    DeferredPackagePath           = PackagePath;
    DeferredSourceFilename        = Filename;
    DeferredSourceHash            = SourceHash;
    bDeferredWantsAssign          = bWantsAssign;
    bDeferredOverwriteABP         = bWantsABPOverwrite;
    bDeferredOverwriteSpringAsset = bWantsOverwrite;
    bDeferredReuseABP             = bWantsReuseABP;
    DeferredSpringDataTransient.Reset(TransientSpringData);

    // Location for ABP
    DeferredAnimFolder = PackagePath / AnimationSubFolder;

    const bool bAnythingToDo = (bWantsSpringData && DeferredSpringDataTransient.IsValid()) || bWantsABP;
    if (bAnythingToDo)
    {
        RegisterPostImportCommit();
    }
#endif
}

#if WITH_EDITOR
void UVRMSpringBonesPostImportPipeline::BeginDestroy()
{
    UnregisterPostImportCommit();
    Super::BeginDestroy();
}
#endif

// ParseAndFillDataAssetFromFile
// - Parses spring config from the source file into a runtime asset container
// - The container is transient here; materialization happens in OnAssetPostImport
bool UVRMSpringBonesPostImportPipeline::ParseAndFillDataAssetFromFile(const FString& Filename, UVRMSpringBoneData* Dest) const
{
    if (!Dest) return false;
    FVRMSpringConfig Config; 
    TMap<int32, int32> NodeParent;
    TMap<int32, FVRMNodeChildren> NodeChildren;
    FString Err;
    TMap<int32, FName> NodeMap;

    bool bParsed =
        VRM::ParseSpringBonesFromFile(Filename, Config, NodeMap, NodeParent, NodeChildren, Err) ||
        VRM::ParseSpringBonesFromFile(Filename, Config, NodeMap, Err) ||
        VRM::ParseSpringBonesFromFile(Filename, Config, Err);

    if (!bParsed)
    {
        return false;
    }

    Dest->SpringConfig = MoveTemp(Config);
    if (NodeMap.Num() > 0)     { Dest->SetNodeToBoneMapping(NodeMap); }
    if (NodeParent.Num() > 0)  { Dest->NodeParent   = MoveTemp(NodeParent); }
    if (NodeChildren.Num() > 0){ Dest->NodeChildren = MoveTemp(NodeChildren); }
    if (Dest->NodeChildren.Num() > 0)
    {
        Dest->BuildResolvedChildren();
    }

    return Dest->SpringConfig.IsValid();
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
    auto GetNodeName = [&](int32 NodeIndex)->FName
    {
        if(NodeIndex<0||NodeIndex>=NodesCount) return NAME_None;
        const cgltf_node* N=&Data->nodes[NodeIndex];
        return (N&&N->name&&N->name[0])?FName(UTF8_TO_TCHAR(N->name)):NAME_None;
    };
    for (FVRMSpringCollider& C : InOut.Colliders) if (C.BoneName.IsNone() && C.NodeIndex!=INDEX_NONE){ if(FName Nm=GetNodeName(C.NodeIndex); !Nm.IsNone()){ C.BoneName=Nm; ++OutResolvedColliders; }}
    for (FVRMSpringJoint& J : InOut.Joints)    if (J.BoneName.IsNone() && J.NodeIndex!=INDEX_NONE){ if(FName Nm=GetNodeName(J.NodeIndex); !Nm.IsNone()){ J.BoneName=Nm; ++OutResolvedJoints; }}
    for (FVRMSpring& S : InOut.Springs)        if (S.CenterBoneName.IsNone() && S.CenterNodeIndex!=INDEX_NONE){ if(FName Nm=GetNodeName(S.CenterNodeIndex); !Nm.IsNone()){ S.CenterBoneName=Nm; ++OutResolvedCenters; }}
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
        FARFilter MeshFilter; 
        MeshFilter.bRecursivePaths=true;
        MeshFilter.PackagePaths.Add(*SearchRootPackagePath);
        MeshFilter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
        TArray<FAssetData> Meshes; ARM.Get().GetAssets(MeshFilter, Meshes);
        if (Meshes.Num()>0) if (USkeletalMesh* SM=Cast<USkeletalMesh>(Meshes[0].GetAsset())) Skeleton=SM->GetSkeleton();
    }
    if (!Skeleton) return;

    const FReferenceSkeleton& RefSkel = Skeleton->GetReferenceSkeleton();
    TSet<FName> Valid; for(int32 i=0;i<RefSkel.GetNum();++i) Valid.Add(RefSkel.GetBoneName(i));

    auto Report=[&](const TArray<FName>& Missing,const TCHAR* What)
    {
        if(Missing.Num()>0)
        {
            UE_LOG(LogVRMSpring,Warning,TEXT("[VRMInterchange] Spring pipeline: %d %s not on skeleton '%s'"),Missing.Num(),What,*Skeleton->GetPathName());
        }
    };

    TArray<FName> MC, MJ, MCent;
    for(const FVRMSpringCollider& C:Config.Colliders) if(!C.BoneName.IsNone()&&!Valid.Contains(C.BoneName)) MC.AddUnique(C.BoneName);
    for(const FVRMSpringJoint& J:Config.Joints) if(!J.BoneName.IsNone()&&!Valid.Contains(J.BoneName)) MJ.AddUnique(J.BoneName);
    for(const FVRMSpring& S:Config.Springs) if(!S.CenterBoneName.IsNone()&&!Valid.Contains(S.CenterBoneName)) MCent.AddUnique(S.CenterBoneName);
    Report(MC,TEXT("collider BoneName(s)"));
    Report(MJ,TEXT("joint BoneName(s)"));
    Report(MCent,TEXT("center BoneName(s)"));
}

void UVRMSpringBonesPostImportPipeline::ConvertSpringConfigToUEUnits(FVRMSpringConfig& InOut) const
{
    const float Scale = 100.0f; // meters -> centimeters
    // Springs: scale fields that represent distances or accelerations
    for (FVRMSpring& S : InOut.Springs)
    {
        S.HitRadius   *= Scale; // length
        S.GravityPower *= Scale; // acceleration magnitude (m/s^2 -> cm/s^2)
        // Stiffness/Drag/GravityDir remain unchanged
    }

    // Colliders: scale offsets and radii
    for (FVRMSpringCollider& Col : InOut.Colliders)
    {
        for (FVRMSpringColliderSphere& S : Col.Spheres)
        {
            S.Offset *= Scale;
            S.Radius *= Scale;
        }
        for (FVRMSpringColliderCapsule& Cap : Col.Capsules)
        {
            Cap.Offset     *= Scale;
            Cap.TailOffset *= Scale;
            Cap.Radius     *= Scale;
        }
        for (FVRMSpringColliderPlane& P : Col.Planes)
        {
            P.Offset *= Scale;
            // P.Normal is unit-length direction, no scaling
        }
    }
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

// Duplicate a template Post-Process AnimBlueprint into the target folder.
// Does not save; marks package dirty and compiles the new ABP to ensure GeneratedClass is valid.
UObject* UVRMSpringBonesPostImportPipeline::DuplicateTemplateAnimBlueprint(const FString& TargetPackagePath, const FString& BaseName, USkeleton* TargetSkeleton, bool bOverwriteExistingABP) const
{
    if(!TargetSkeleton) return nullptr;

    const TCHAR* TemplatePath=TEXT("/VRMInterchange/Animation/ABP_VRMSpringBones_Template.ABP_VRMSpringBones_Template");
    UAnimBlueprint* TemplateABP=Cast<UAnimBlueprint>(StaticLoadObject(UAnimBlueprint::StaticClass(),nullptr,TemplatePath));
    if(!TemplateABP)
    {
        UE_LOG(LogVRMSpring,Warning,TEXT("[VRMInterchange] Spring pipeline: Could not find template ABP at '%s'."),TemplatePath);
        return nullptr;
    }

    FString NewAssetPath=TargetPackagePath/ BaseName;
    FAssetToolsModule& AssetToolsModule=FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
    FString UniquePath,UniqueName;

    if (!bOverwriteExistingABP)
    {
        AssetToolsModule.Get().CreateUniqueAssetName(NewAssetPath,TEXT(""),UniquePath,UniqueName);
    }
    else
    {
        UniquePath = NewAssetPath;
        UniqueName = BaseName;
    }

    const FString LongPackage=UniquePath.StartsWith(TEXT("/"))?UniquePath:TEXT("/")+UniquePath;
    UPackage* Pkg=CreatePackage(*LongPackage);
    if(!Pkg) return nullptr;
    UObject* Duplicated=StaticDuplicateObject(TemplateABP,Pkg,*UniqueName);
    if(!Duplicated)
    {
        UE_LOG(LogVRMSpring,Warning,TEXT("[VRMInterchange] Spring pipeline: Failed to duplicate template ABP."));
        return nullptr;
    }
    FAssetRegistryModule::AssetCreated(Duplicated);
    if(UAnimBlueprint* NewABP=Cast<UAnimBlueprint>(Duplicated))
    {
        NewABP->TargetSkeleton=TargetSkeleton;
        FKismetEditorUtilities::CompileBlueprint(NewABP);
    }
    return Duplicated;
}

// Tries to set SpringConfig (object or soft object) on the ABP CDO and mark dirty.
bool UVRMSpringBonesPostImportPipeline::SetSpringConfigOnAnimBlueprint(UObject* AnimBlueprintObj, UVRMSpringBoneData* SpringData) const
{
    if(!AnimBlueprintObj||!SpringData) return false;

    if(UAnimBlueprint* ABP=Cast<UAnimBlueprint>(AnimBlueprintObj))
    {
        if(!ABP->GeneratedClass){ FKismetEditorUtilities::CompileBlueprint(ABP);} 
        UAnimBlueprintGeneratedClass* GenClass=Cast<UAnimBlueprintGeneratedClass>(ABP->GeneratedClass);
        if(!GenClass) return false;
        UObject* CDO=GenClass->GetDefaultObject();
        if(!CDO) return false;

        if(FObjectProperty* ObjProp=FindFProperty<FObjectProperty>(GenClass,TEXT("SpringConfig")))
        {
            ObjProp->SetObjectPropertyValue_InContainer(CDO,SpringData);
        }
        else if(FSoftObjectProperty* SoftProp=FindFProperty<FSoftObjectProperty>(GenClass,TEXT("SpringConfig")))
        {
            SoftProp->SetObjectPropertyValue_InContainer(CDO,SpringData);
        }
        else
        {
            return false;
        }
        CDO->Modify();
        CDO->MarkPackageDirty();
        ABP->MarkPackageDirty();
        return true;
    }

    if(UClass* BPClass=Cast<UClass>(AnimBlueprintObj))
    {
        UObject* CDO=BPClass->GetDefaultObject();
        if(!CDO) return false;

        if(FObjectProperty* ObjProp=FindFProperty<FObjectProperty>(BPClass,TEXT("SpringConfig")))
        {
            ObjProp->SetObjectPropertyValue_InContainer(CDO,SpringData);
        }
        else if(FSoftObjectProperty* SoftProp=FindFProperty<FSoftObjectProperty>(BPClass,TEXT("SpringConfig")))
        {
            SoftProp->SetObjectPropertyValue_InContainer(CDO,SpringData);
        }
        else
        {
            return false;
        }

        CDO->Modify();
        CDO->MarkPackageDirty();
        return true;
    }

    return false;
}

bool UVRMSpringBonesPostImportPipeline::AssignPostProcessABPToMesh(USkeletalMesh* SkelMesh, UObject* AnimBlueprintObj) const
{
    if(!SkelMesh||!AnimBlueprintObj) return false;
    UClass* BPClass=nullptr;
    if(UAnimBlueprint* ABP=Cast<UAnimBlueprint>(AnimBlueprintObj))
    {
        FKismetEditorUtilities::CompileBlueprint(ABP);
        BPClass=ABP->GeneratedClass;
    }
    else
    {
        BPClass=Cast<UClass>(AnimBlueprintObj);
    }
    if(!BPClass) return false;
    SkelMesh->SetPostProcessAnimBlueprint(BPClass);
    SkelMesh->MarkPackageDirty();
    return true;
}

FString UVRMSpringBonesPostImportPipeline::GetParentPackagePath(const FString& InPath) const
{
    int32 SlashIdx=INDEX_NONE;
    return (InPath.FindLastChar(TEXT('/'),SlashIdx)&&SlashIdx>1)?InPath.Left(SlashIdx):InPath;
}

// OnAssetPostImport
// - After user accepts the import dialog, skeletal assets exist.
// - Materialize SpringData and optional ABP; assign ABP to mesh if requested.
// - Do not save packages; leave them dirty for the editor Save/SCC flow.
void UVRMSpringBonesPostImportPipeline::OnAssetPostImport(UFactory* InFactory, UObject* InCreatedObject)
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

    USkeletalMesh* SkelMesh = nullptr;
    USkeleton* Skeleton = nullptr;
    bool bFound = FindImportedSkeletalAssets(DeferredSkeletonSearchRoot, SkelMesh, Skeleton) && (SkelMesh || Skeleton);
    if (!bFound)
    {
        bFound = FindImportedSkeletalAssets(DeferredAltSkeletonSearchRoot, SkelMesh, Skeleton) && (SkelMesh || Skeleton);
    }
    if (!bFound)
    {
        return;
    }

    // 1) Spring data asset
    UVRMSpringBoneData* SpringDataAsset = nullptr;
    if (DeferredSpringDataTransient.IsValid())
    {
        // Use the staged character base path; name derives from the actual SkeletalMesh.
        const FString PackagePath = DeferredPackagePath;

        FString SpringAssetName = SkelMesh
            ? (SkelMesh->GetName() + TEXT("_SpringData"))
            : (FPaths::GetBaseFilename(DeferredSourceFilename) + TEXT("_SpringData"));

        FString SpringDataFolder = PackagePath;
        if (!SubFolder.IsEmpty())
        {
            SpringDataFolder /= SubFolder;
        }

        FString FinalSpringPackageName = SpringDataFolder / SpringAssetName;
        if (!bDeferredOverwriteSpringAsset)
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
        if (SpringPackage)
        {
            SpringDataAsset = NewObject<UVRMSpringBoneData>(SpringPackage, *SpringAssetName, RF_Public | RF_Standalone);
            SpringDataAsset->SpringConfig = DeferredSpringDataTransient->SpringConfig;
            SpringDataAsset->NodeParent   = DeferredSpringDataTransient->NodeParent;
            SpringDataAsset->NodeChildren = DeferredSpringDataTransient->NodeChildren;
            SpringDataAsset->SetNodeToBoneMapping(DeferredSpringDataTransient->NodeToBoneMap);
            if (SpringDataAsset->NodeChildren.Num() > 0)
            {
                SpringDataAsset->BuildResolvedChildren();
            }
            SpringDataAsset->SourceFilename = DeferredSpringDataTransient->SourceFilename;
            SpringDataAsset->SourceHash     = DeferredSpringDataTransient->SourceHash;

#if WITH_EDITOR
            FVRMInterchangeEditorModule::NotifySpringDataCreated(SpringDataAsset);
            FAssetRegistryModule::AssetCreated(SpringDataAsset);
#endif
            SpringDataAsset->MarkPackageDirty();
            SpringPackage->SetDirtyFlag(true);
        }
    }

    // 2) ABP create/reuse and assignment
    const bool bWantsABP = (bGeneratePostProcessAnimBP || (GetDefault<UVRMInterchangeSettings>()->bGeneratePostProcessAnimBP));
    if (bWantsABP)
    {
        FString CharName = SkelMesh ? SkelMesh->GetName() : FPaths::GetBaseFilename(DeferredSourceFilename);
        const FString EffectiveABPName = FString::Printf(TEXT("PP_ABP_VRMSpringBones_%s"), *CharName);

        UObject* ReusedOrDuplicatedABP = nullptr;

        if (bDeferredReuseABP)
        {
            FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
            FARFilter Filter;
            Filter.bRecursivePaths = false;
            Filter.PackagePaths.Add(*DeferredAnimFolder);
            Filter.ClassPaths.Add(UAnimBlueprint::StaticClass()->GetClassPathName());
            TArray<FAssetData> Found; ARM.Get().GetAssets(Filter, Found);
            if (Found.Num() > 0)
            {
                for (const FAssetData& AD : Found)
                {
                    if (AD.AssetName.ToString().Equals(EffectiveABPName, ESearchCase::IgnoreCase))
                    {
                        ReusedOrDuplicatedABP = AD.GetAsset();
                        break;
                    }
                }
                if (!ReusedOrDuplicatedABP)
                {
                    ReusedOrDuplicatedABP = Found[0].GetAsset();
                }

                if (ReusedOrDuplicatedABP && SpringDataAsset)
                {
                    if (!SetSpringConfigOnAnimBlueprint(ReusedOrDuplicatedABP, SpringDataAsset))
                    {
                        UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Failed to set SpringConfig on existing ABP."));
                        ReusedOrDuplicatedABP = nullptr;
                    }
                }
            }
        }

        if (!ReusedOrDuplicatedABP)
        {
            ReusedOrDuplicatedABP = DuplicateTemplateAnimBlueprint(
                DeferredAnimFolder,
                EffectiveABPName,
                Skeleton ? Skeleton : (SkelMesh ? SkelMesh->GetSkeleton() : nullptr),
                bDeferredOverwriteABP
            );
        }

        if (ReusedOrDuplicatedABP && SpringDataAsset)
        {
            if (!SetSpringConfigOnAnimBlueprint(ReusedOrDuplicatedABP, SpringDataAsset))
            {
                UE_LOG(LogVRMSpring, Warning, TEXT("[VRMInterchange] Spring pipeline: Failed to set SpringConfig on duplicated ABP."));
            }
        }
        if (ReusedOrDuplicatedABP && bDeferredWantsAssign && SkelMesh)
        {
            AssignPostProcessABPToMesh(SkelMesh, ReusedOrDuplicatedABP);
            SkelMesh->MarkPackageDirty();
        }
        if (ReusedOrDuplicatedABP)
        {
            ReusedOrDuplicatedABP->MarkPackageDirty();
        }
    }

    bDeferredCompleted = true;
    UnregisterPostImportCommit();
    DeferredSpringDataTransient.Reset();
    UE_LOG(LogVRMSpring, Log, TEXT("[VRMInterchange] Spring pipeline: Post-import commit completed (no save)."));
}

#if WITH_EDITOR
void UVRMSpringBonesPostImportPipeline::RegisterPostImportCommit()
{
    if (ImportPostHandle.IsValid())
    {
        return;
    }
    if (UImportSubsystem* ImportSubsystem = GEditor ? GEditor->GetEditorSubsystem<UImportSubsystem>() : nullptr)
    {
        ImportPostHandle = ImportSubsystem->OnAssetPostImport.AddUObject(
            this, &UVRMSpringBonesPostImportPipeline::OnAssetPostImport);
    }
}

void UVRMSpringBonesPostImportPipeline::UnregisterPostImportCommit()
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
#endif