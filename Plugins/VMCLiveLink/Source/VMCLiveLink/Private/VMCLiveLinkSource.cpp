#include "VMCLiveLinkSource.h"
#include "VMCLog.h"

// Live Link
#include "ILiveLinkClient.h"
#include "LiveLinkTypes.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"

#include "VMCLiveLinkSettings.h"
#include "LiveLinkSubjectSettings.h"
#include "LiveLinkSubjectRemapper.h"
#include "VMCLiveLinkRemapper.h"
#include "Async/Async.h"


// OSC (cpp-only)
#include "OSCServer.h"
#include "OSCMessage.h"
#include "OSCTypes.h"

// Math
#include "Math/RotationMatrix.h"

// ---------------- Utils: OSC argument reading (UE5.6) ----------------

static bool ReadStringFloat7(const FOSCMessage& Msg, FString& OutName,
    float& px, float& py, float& pz,
    float& qx, float& qy, float& qz, float& qw)
{
    const TArray<UE::OSC::FOSCData>& A = Msg.GetArgumentsChecked();
    if (A.Num() != 8) return false;
    OutName = A[0].GetString();
    px = A[1].GetFloat(); py = A[2].GetFloat(); pz = A[3].GetFloat();
    qx = A[4].GetFloat(); qy = A[5].GetFloat(); qz = A[6].GetFloat(); qw = A[7].GetFloat();
    return true;
}

static bool ReadFloat7(const FOSCMessage& Msg,
    float& px, float& py, float& pz,
    float& qx, float& qy, float& qz, float& qw)
{
    const TArray<UE::OSC::FOSCData>& A = Msg.GetArgumentsChecked();
    if (A.Num() != 7) return false;
    px = A[0].GetFloat();
    py = A[1].GetFloat();
    pz = A[2].GetFloat();
    
    qx = A[3].GetFloat();
    qy = A[4].GetFloat();
    qz = A[5].GetFloat();
    qw = A[6].GetFloat();
    
    return true;
}

// ---------------- Ctors & status ----------------

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName)
    : SourceName(InSourceName), ListenPort(39539), bUnityToUE(true), bMetersToCm(true), YawOffsetDeg(0.f)
{
}

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName, int32 InPort)
    : SourceName(InSourceName), ListenPort(InPort), bUnityToUE(true), bMetersToCm(true), YawOffsetDeg(0.f)
{
}

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg)
    : SourceName(InSourceName), ListenPort(InPort), bUnityToUE(bInUnityToUE), bMetersToCm(bInMetersToCm), YawOffsetDeg(InYawDeg)
{
}

FVMCLiveLinkSource::FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg, FString InSubject)
    : SourceName(InSourceName), ListenPort(InPort), bUnityToUE(bInUnityToUE), bMetersToCm(bInMetersToCm), YawOffsetDeg(InYawDeg), SubjectName(InSubject)

{
}

void FVMCLiveLinkSource::ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid)
{
    Client = InClient;
    SourceGuid = InSourceGuid;
    bIsValid = StartOSC();
    AsyncTask(ENamedThreads::GameThread, [this]()
    {
        EnsureSubjectSettingsWithDefaults();
    });
    // Warm caches and republish static once with mapped names
    RefreshStaticMapsFromSettings();
    bForceStaticNext = true;

    UE_LOG(LogVMCLiveLink, Log, TEXT("VMC source '%s' listening on %d (valid=%d, unity2ue=%d, m_to_cm=%d, yaw=%.1f)"),
        *SourceName, ListenPort, bIsValid ? 1 : 0, bUnityToUE ? 1 : 0, bMetersToCm ? 1 : 0, YawOffsetDeg);
}

bool FVMCLiveLinkSource::RequestSourceShutdown()
{
    StopOSC();
    bIsValid = false;
    Client = nullptr;
    return true;
}

FText FVMCLiveLinkSource::GetSourceStatus() const
{
    const bool bReady = bIsValid && bStaticSent;
    return bIsValid
        ? (bReady
            ? NSLOCTEXT("VMCLiveLink", "Status_Receiving", "Receiving data")
            : NSLOCTEXT("VMCLiveLink", "Status_Waiting", "Waiting for first frame"))
        : NSLOCTEXT("VMCLiveLink", "Status_Stopped", "Stopped");
}

// ---------------- OSC lifecycle ----------------

bool FVMCLiveLinkSource::StartOSC()
{
    if (OscServer.IsValid())
        return true;

    OscServer = TStrongObjectPtr<UOSCServer>(NewObject<UOSCServer>());
    if (!OscServer.IsValid())
    {
        UE_LOG(LogVMCLiveLink, Error, TEXT("Failed to create UOSCServer"));
        return false;
    }

    OscServer->OnOscMessageReceivedNative.AddRaw(this, &FVMCLiveLinkSource::OnOscMessageReceived);

    if (!OscServer->SetAddress(TEXT("0.0.0.0"), (uint16)ListenPort))
    {
        UE_LOG(LogVMCLiveLink, Error, TEXT("UOSCServer SetAddress failed for port %d"), ListenPort);
        OscServer->OnOscMessageReceivedNative.RemoveAll(this);
        OscServer.Reset();
        return false;
    }

    OscServer->Listen();
    return true;
}

void FVMCLiveLinkSource::StopOSC()
{
    if (OscServer.IsValid())
    {
        OscServer->OnOscMessageReceivedNative.RemoveAll(this);
        OscServer->Stop();
        OscServer.Reset();
    }
}



// ---------------- OSC message handler ----------------

void FVMCLiveLinkSource::OnOscMessageReceived(const FOSCMessage& Msg, const FString& FromIP, uint16 FromPort)
{
    const FString Addr = Msg.GetAddress().GetFullPath();

    if (Addr == TEXT("/VMC/Ext/Bone/Pos"))
    {
        FString Bone; float px, py, pz, qx, qy, qz, qw;
        if (!ReadStringFloat7(Msg, Bone, px, py, pz, qx, qy, qz, qw))
            return;

        const FName BoneName(*Bone);

        // Unity -> UE5 Conversions

        const float upx = -px * 100.f;
        const float upy = pz * 100.f;
        const float upz = py * 100.f;

        const float uqx = -qx;
        const float uqy = qz;
        const float uqz = qy;
        const float uqw = qw;

        FVector P = bUnityToUE ? FVector(upx, upy, upz)
            : FVector(px, py, pz);
        FQuat   Q = bUnityToUE ? FQuat(uqx, uqy, uqz, uqw)
            : FQuat(qx, qy, qz, qw);

        const FTransform Xf(Q, P, FVector(1));

        FScopeLock Lock(&DataGuard);
        if (!BoneNames.Contains(BoneName))
        {
            // Default: first seen bone becomes root (-1 parent). Others parent to 0 unless explicitly "root".
            int32 Parent = BoneNames.Num() == 0 ? -1 : 0;
            if (BoneName == FName("root")) Parent = -1;
            BoneNames.Add(BoneName);
            BoneParents.Add(Parent);
        }
        PendingPose.Add(BoneName, Xf);
    }
    else if (Addr == TEXT("/VMC/Ext/Root/Pos"))
    {
        float px, py, pz, qx, qy, qz, qw;
        if (!ReadFloat7(Msg, px, py, pz, qx, qy, qz, qw))
            return;

        // Unity -> UE5 Conversions
        const float upx = -px * 100.f;
        const float upy = pz * 100.f;
        const float upz = py * 100.f;

        const float uqx = -qx;
        const float uqy = qz;
        const float uqz = qy;
        const float uqw = qw;

        FVector PR = bUnityToUE ? FVector(upx, upy, upz)
            : FVector(px, py, pz);
        FQuat   QR = bUnityToUE ? FQuat(uqx, uqy, uqz, uqw)
            : FQuat(qx, qy, qz, qw);

        {
            FScopeLock Lock(&DataGuard);
            PendingRoot = FTransform(QR, PR, FVector(1));

            // Ensure a 'root' exists in the skeleton so we have a slot to apply it
            if (!BoneNames.Contains(FName("root")))
            {
                BoneNames.Insert(FName("root"), 0);
                BoneParents.Insert(-1, 0);
            }
        }
    }
    else if (Addr == TEXT("/VMC/Ext/Blend/Val"))
    {
        const TArray<UE::OSC::FOSCData>& A = Msg.GetArgumentsChecked();
        if (A.Num() != 2) return;

        const FName CurveName(*A[0].GetString());
        const float Val = A[1].GetFloat();

        FScopeLock Lock(&DataGuard);
        if (!CurveNameToIndex.Contains(CurveName))
        {
            const int32 NewIdx = CurveNamesOrdered.Add(CurveName);
            CurveNameToIndex.Add(CurveName, NewIdx);
            bStaticCurvesDirty = true; // advertise this name in static data
        }
        PendingCurves.Add(CurveName, Val);
    }
    else if (Addr == TEXT("/VMC/Ext/Blend/Apply"))
    {
        AsyncTask(ENamedThreads::GameThread, [this]()
        {
            // If we haven’t yet attached defaults, do it now.
            if (!bEnsuredDefaults)
            {
                EnsureSubjectSettingsWithDefaults();
            }
        });
        // Pull latest maps from the subject settings the preset applied
        RefreshStaticMapsFromSettings();

        // If we just attached or maps changed, republish static once
        if (bForceStaticNext)
        {
            bForceStaticNext = false;
            PushStaticData(/*bForce=*/true);
        }

        // Existing flow
        PushStaticData(/*bForce=*/false);
        PushFrame();

        FScopeLock Lock(&DataGuard);
        PendingCurves.Reset();

        if (bStaticCurvesDirty)
        {
            bStaticCurvesDirty = false;
            PushStaticData(/*bForce=*/true);
        }
    }

}

// ---------------- Live Link data push ----------------

void FVMCLiveLinkSource::PushStaticData(bool bForce)
{
    FScopeLock Lock(&DataGuard);

    const bool bHaveBones = BoneNames.Num() > 0;
    if (!Client || (!bForce && (bStaticSent || !bHaveBones)))
    {
        return;
    }

    // Make editable copies
// Make editable copies
    TArray<FName> OutBoneNames = BoneNames;
    TArray<FName> OutCurveNames = CurveNamesOrdered;

    // Apply cached maps (preserve order → indices remain valid)
    for (FName& N : OutBoneNames)  if (const FName* M = CachedBoneMap.Find(N))  N = *M;
    for (FName& C : OutCurveNames) if (const FName* M = CachedCurveMap.Find(C)) C = *M;

    // Build/push StaticData as you already do (bones, parents, PropertyNames)

    // Apply cached maps (order preserved → indices stay valid)
    if (CachedBoneMap.Num() > 0)
    {
        for (FName& N : OutBoneNames)
            if (const FName* M = CachedBoneMap.Find(N)) N = *M;
    }
    if (CachedCurveMap.Num() > 0)
    {
        for (FName& C : OutCurveNames)
            if (const FName* M = CachedCurveMap.Find(C)) C = *M;
    }

    // Build static packet
    FLiveLinkStaticDataStruct StaticData(FLiveLinkSkeletonStaticData::StaticStruct());
    auto& Skel = *StaticData.Cast<FLiveLinkSkeletonStaticData>();

    Skel.SetBoneNames(OutBoneNames);
    Skel.SetBoneParents(BoneParents);

    // UE 5.6: curve names live on the base static data array
    Skel.PropertyNames = OutCurveNames;



    Client->PushSubjectStaticData_AnyThread({ SourceGuid, SubjectName },
        ULiveLinkAnimationRole::StaticClass(), MoveTemp(StaticData));

    bStaticSent = true;
}


void FVMCLiveLinkSource::PushFrame()
{
    if (!Client) return;

    // Snapshot state under lock
    TArray<FName>            LocalBoneNames;
    TArray<int32>            LocalBoneParents;
    TMap<FName, FTransform>  LocalPose;
    FTransform               LocalRoot = FTransform::Identity;
    TArray<FName>            LocalCurveNames;
    TMap<FName, int32>        LocalCurveNameToIndex;
    TMap<FName, float>        LocalCurves;

    // Optional helpers (added earlier in this thread)
    TMap<FName, FName>       LocalBoneMap;                 // source → mapped
    TMap<FName, FVector>     LocalRefOffsets;              // mapped name → ref local translation
    bool                     bLocalUseRefOffsets = true;
    bool                     bLocalPreferIncoming = false;
    bool                     bLocalHaveRefOffsets = false;

    {
        FScopeLock Lock(&DataGuard);
        LocalBoneNames = BoneNames;
        LocalBoneParents = BoneParents;
        LocalPose = PendingPose;
        LocalRoot = PendingRoot;
        LocalCurveNames = CurveNamesOrdered;
        LocalCurveNameToIndex = CurveNameToIndex;
        LocalCurves = PendingCurves;

        // if you added these members (recommended), we pull them
        LocalBoneMap = CachedBoneMap;
        LocalRefOffsets = RefLocalTranslationByName;
        bLocalUseRefOffsets = bUseRefOffsets;
        bLocalPreferIncoming = bPreferIncomingTranslations;
        bLocalHaveRefOffsets = bHaveRefOffsets;
    }

    // Build frame payload
    FLiveLinkFrameDataStruct Frame(FLiveLinkAnimationFrameData::StaticStruct());
    auto& Anim = *Frame.Cast<FLiveLinkAnimationFrameData>();
    FLiveLinkBaseFrameData& Base = static_cast<FLiveLinkBaseFrameData&>(Anim);

    const int32 NumBones = LocalBoneNames.Num();
    const int32 NumCurves = LocalCurveNames.Num();

    Anim.Transforms.SetNum(NumBones);
    Base.PropertyValues.SetNum(NumCurves);
    for (int32 i = 0; i < NumCurves; ++i) Base.PropertyValues[i] = 0.f;

    auto MapBoneName = [&](const FName& Src)->FName
        {
            if (const FName* M = LocalBoneMap.Find(Src)) return *M;
            return Src;
        };

    // Fill transforms as LOCAL (parent-space) per Live Link Animation Role
    for (int32 i = 0; i < NumBones; ++i)
    {
        const FName SrcName = LocalBoneNames[i];
        FTransform X = FTransform::Identity;

        // Rotation from incoming stream (if present)
        if (const FTransform* In = LocalPose.Find(SrcName))
        {
            X.SetRotation(In->GetRotation());
            if (bLocalPreferIncoming) // only if your stream provides proper local translations
                X.SetTranslation(In->GetTranslation());
        }

        // Translation handling
        if (LocalBoneParents.IsValidIndex(i) && LocalBoneParents[i] == -1)
        {
            // Root gets live root translation
            const FTransform* In = LocalPose.Find(SrcName);
            X.SetTranslation(In->GetTranslation());
        }
        else
        {
            // Non-root: use ref-pose local offsets unless we truly trust incoming translations
            if (!bLocalPreferIncoming || X.GetTranslation().IsNearlyZero())
            {
                if (bLocalUseRefOffsets && bLocalHaveRefOffsets)
                {
                    const FName Mapped = MapBoneName(SrcName);
                    if (const FVector* Off = LocalRefOffsets.Find(Mapped))
                    {
                        X.SetTranslation(*Off);
                    }
                }
            }
        }

        Anim.Transforms[i] = X;
    }

    // Curves → PropertyValues using fixed order
    for (const TPair<FName, float>& KV : LocalCurves)
    {
        if (const int32* Idx = LocalCurveNameToIndex.Find(KV.Key))
        {
            const int32 I = *Idx;
            if (Base.PropertyValues.IsValidIndex(I))
            {
                Base.PropertyValues[I] = KV.Value;
            }
        }
    }

    Client->PushSubjectFrameData_AnyThread({ SourceGuid, SubjectName }, MoveTemp(Frame));
}


uint32 FVMCLiveLinkSource::HashMaps(const TMap<FName, FName>& A, const TMap<FName, FName>& B)
{
    uint32 H = 1469598103u; // FNV-ish seed
    auto Mix = [&](const TMap<FName, FName>& M)
        {
            for (const auto& P : M)
            {
                H = HashCombine(H, GetTypeHash(P.Key));
                H = HashCombine(H, GetTypeHash(P.Value));
            }
        };
    Mix(A); Mix(B);
    return H;
}

void FVMCLiveLinkSource::RefreshStaticMapsIfNeeded()
{
    UVMCLiveLinkRemapper* R = StaticNameRemapper.LoadSynchronous();
    if (!R) return;

    const uint32 NewHash = HashMaps(R->BoneNameMap, R->CurveNameMap);
    if (NewHash != CachedMapsHash)
    {
        CachedMapsHash = NewHash;
        CachedBoneMap = R->BoneNameMap;
        CachedCurveMap = R->CurveNameMap;

        // Force one static publish on next /Apply to propagate new names
        bForceStaticNext = true;
    }
}

void FVMCLiveLinkSource::BuildRefOffsetsFromMesh(USkeletalMesh* Mesh)
{
    RefLocalTranslationByName.Empty();
    bHaveRefOffsets = false;
    if (!Mesh) return;

    const FReferenceSkeleton& RS = Mesh->GetRefSkeleton();
    const TArray<FTransform>& RefPose = RS.GetRefBonePose(); // local (parent-space)
    const int32 Num = RS.GetNum();
    for (int32 i = 0; i < Num; ++i)
    {
        const FName Bone = RS.GetBoneName(i);
        RefLocalTranslationByName.Add(Bone, RefPose[i].GetTranslation());
    }
    bHaveRefOffsets = true;
}

// Pull remapper + maps + ReferenceSkeleton from subject settings
void FVMCLiveLinkSource::RefreshStaticMapsFromSettings()
{
    if (!Client) return;

    UObject* SettingsObj = Client->GetSubjectSettings({ SourceGuid, SubjectName });
    ULiveLinkSubjectSettings* Settings = Cast<ULiveLinkSubjectSettings>(SettingsObj);
    ULiveLinkSubjectRemapper* NowRemapper = Settings ? Settings->Remapper : nullptr;

    const bool bRemapperChanged = (LastSeenRemapper.Get() != NowRemapper);
    if (bRemapperChanged)
    {
        LastSeenRemapper = NowRemapper;
        bForceStaticNext = true; // names may change
    }

    // Pull maps + reference mesh
    TMap<FName, FName> NewBone, NewCurve;
    USkeletalMesh* RefMesh = nullptr;

    if (NowRemapper)
    {
        NewBone = NowRemapper->BoneNameMap;

        if (const UVMCLiveLinkRemapper* My = Cast<UVMCLiveLinkRemapper>(NowRemapper))
        {
            NewCurve = My->CurveNameMap;
            RefMesh = My->ReferenceSkeleton.LoadSynchronous();
        }
    }

    //  - Rebuild offsets if:
    //  - mesh changed
    //  - or we never built them
    //  - or our cache looks incomplete vs the mesh
    const bool bMeshChanged = (LastRefMeshBuiltFrom.Get() != RefMesh);
    const bool bNeverBuilt = !bHaveRefOffsets || RefLocalTranslationByName.Num() == 0;
    const bool bCountMismatch = RefMesh && (RefLocalTranslationByName.Num() != RefMesh->GetRefSkeleton().GetNum());

    if (RefMesh && (bMeshChanged || bNeverBuilt || bCountMismatch))
    {
        BuildRefOffsetsFromMesh(RefMesh);
        LastRefMeshBuiltFrom = RefMesh;
        // No need to force static here; offsets affect frames, not names.
    }

    const uint32 NewHash = HashMaps(NewBone, NewCurve);
    if (NewHash != CachedMapsHash)
    {
        CachedMapsHash = NewHash;
        CachedBoneMap = MoveTemp(NewBone);
        CachedCurveMap = MoveTemp(NewCurve);
        bForceStaticNext = true; // names changed → republish once
    }
}

void FVMCLiveLinkSource::EnsureSubjectSettingsWithDefaults()
{
    if (!Client || bEnsuredDefaults)
        return;

    check(IsInGameThread());

    const FLiveLinkSubjectKey Key{ SourceGuid, SubjectName };

    // 1) Bootstrap the subject settings if they don't exist yet.

    FLiveLinkSubjectPreset Preset;
    Preset.Key = FLiveLinkSubjectKey{ SourceGuid, SubjectName };
    Preset.Role = ULiveLinkAnimationRole::StaticClass();

    // Create a settings object we can hand to the client
    ULiveLinkSubjectSettings* NewSettings = NewObject<ULiveLinkSubjectSettings>(GetTransientPackage());

    const UVMCLiveLinkSettings* Proj = GetDefault<UVMCLiveLinkSettings>();
    UClass* RemapperClass = (Proj && !Proj->DefaultRemapperClass.IsNull())
        ? Proj->DefaultRemapperClass.LoadSynchronous()
        : UVMCLiveLinkRemapper::StaticClass();

    NewSettings->Remapper = NewObject<ULiveLinkSubjectRemapper>(NewSettings, RemapperClass);
    Preset.Settings = NewSettings;

    // This creates the subject + settings in the client now (not “eventually”)
    Client->CreateSubject(Preset);
    Client->SetSubjectEnabled(Preset.Key, true);

    // 3) Warm caches and make sure we publish remapped names once
    RefreshStaticMapsFromSettings();
    bForceStaticNext = true;
    bEnsuredDefaults = true;

    // If bones are already known, push now; otherwise this will early-out harmlessly
    PushStaticData(/*bForce=*/true);
}
