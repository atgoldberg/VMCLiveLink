// Copyright (c) 2025 Shocap Entertainment | Athomas Goldberg. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
#include "ILiveLinkSource.h"
#include "HAL/CriticalSection.h"
#include "UObject/StrongObjectPtr.h"

// Forward declarations (keep OSC headers out of Public/)
class UOSCServer;
struct FOSCMessage;
// forward declare to avoid pulling headers into the .h

class ULiveLinkSubjectRemapper;
class ULiveLinkSubjectSettings;
class UVMCLiveLinkRemapper;

/**
 * VMC → Live Link source (UE 5.6)
 */
class VMCLIVELINK_API FVMCLiveLinkSource
    : public ILiveLinkSource
    , public TSharedFromThis<FVMCLiveLinkSource>
{
public:
    // Constructors
    FVMCLiveLinkSource(const FString& InSourceName);                                  // defaults: port=39539, unity→ue=on, meters→cm=on, yaw=0
    FVMCLiveLinkSource(const FString& InSourceName, int32 InPort);
    FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg);
    FVMCLiveLinkSource(const FString& InSourceName, int32 InPort, bool bInUnityToUE, bool bInMetersToCm, float InYawDeg, FString Subject);

    // ILiveLinkSource
    virtual void ReceiveClient(ILiveLinkClient* InClient, FGuid InSourceGuid) override;
    virtual bool IsSourceStillValid() const override { return bIsValid; }
    virtual bool RequestSourceShutdown() override;

    virtual FText GetSourceType() const override { return NSLOCTEXT("VMCLiveLink", "SourceType", "VMC (OSC)"); }
    virtual FText GetSourceMachineName() const override { return FText::FromString(TEXT("Local/Network")); }
    virtual FText GetSourceStatus() const override;
    // Optional: point this to the same remapper asset you use in Subject Settings (or a duplicate)
    UPROPERTY(EditAnywhere, Category = "Remap")
    TSoftObjectPtr<UVMCLiveLinkRemapper> StaticNameRemapper;

private:
    // OSC lifecycle
    bool StartOSC();
    void StopOSC();
    void OnOscMessageReceived(const FOSCMessage& Message, const FString& IPAddress, uint16 Port);

    // Live Link pushes
    void PushStaticData(bool bForce = false); // bones + property names
    void PushFrame();                         // bone transforms + property values

    // Cached copies of the asset’s maps (so we don’t re-hash every frame)
    TMap<FName, FName> CachedBoneMap;
    TMap<FName, FName> CachedCurveMap;
    uint32 CachedMapsHash = 0;

    // Cached local ref-pose offsets from the remapper’s ReferenceSkeleton
    TMap<FName, FVector> RefLocalTranslationByName;
    bool bHaveRefOffsets = false;

    // Controls
    bool bUseRefOffsets = true;              // ← use ref-pose translations for non-root bones
    bool bPreferIncomingTranslations = false;// ← set true if your stream sends correct local translations

    // One-shot flag to force static re-publish next Apply when maps change
    bool bForceStaticNext = false;

    // Helpers
    void RefreshStaticMapsIfNeeded();
    void RefreshStaticMapsFromSettings(); // (we’ll extend this to also pull the ReferenceSkeleton)
    static uint32 HashMaps(const TMap<FName, FName>& A, const TMap<FName, FName>& B);
    void BuildRefOffsetsFromMesh(class USkeletalMesh* Mesh);

private:
    // Identity / config
    FString SourceName;
    int32   ListenPort = 39539;

    bool  bUnityToUE = true;   // enable basis conversion
    bool  bMetersToCm = true;   // scale positions 1m→100cm
    float YawOffsetDeg = 0.f;    // extra yaw about UE Z (re-express frame; no visible spin)
 
    // Live Link client
    ILiveLinkClient* Client = nullptr;
    FGuid  SourceGuid;
    bool   bIsValid = false;

    // OSC server (UObject) – strong ref so it isn't GC'd
    TStrongObjectPtr<UOSCServer> OscServer;

    // Subject
    FName SubjectName = FName(TEXT("VMC_Subject"));

    // Shared state
    mutable FCriticalSection DataGuard;

    // Static (skeleton) tracking
    bool bStaticSent = false;
    int32 LastRemapVersion = -1;

    // Bones
    TArray<FName> BoneNames;    // index-aligned
    TArray<int32> BoneParents;  // -1 for root

    // Per-frame pose cache
    TMap<FName, FTransform> PendingPose; // bone -> transform
    FTransform PendingRoot = FTransform::Identity;

    // Curves → Properties (UE 5.6)
    TArray<FName>     CurveNamesOrdered;     // advertised in StaticData.PropertyNames
    TMap<FName, int32> CurveNameToIndex;      // name → index in CurveNamesOrdered
    TMap<FName, float> PendingCurves;         // per-frame values
    bool              bStaticCurvesDirty = false; // republish static when set grows
   
    // Track which remapper is currently bound (from subject settings)
    TWeakObjectPtr<ULiveLinkSubjectRemapper> LastSeenRemapper;
    TWeakObjectPtr<USkeletalMesh> LastRefMeshBuiltFrom; // NEW

    private:
        void EnsureSubjectSettingsWithDefaults(); // create settings + attach default remapper/skeleton
        bool bEnsuredDefaults = false;            // NEW: track we've done it once

};
