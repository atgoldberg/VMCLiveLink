// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. / Athomas Goldberg. All Rights Reserved.
﻿// VMCLiveLinkRemapper.h - UE 5.6+
// Remaps bones + curves, optional MetaHuman value shaping, safe refresh (bDirty).

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "ILiveLinkClient.h"
#include "Features/IModularFeatures.h"
#include "LiveLinkSubjectRemapper.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkAnimationTypes.h"
#include "Engine/SkeletalMesh.h"
#include "VMCLiveLinkRemapper.generated.h"

UENUM(BlueprintType)
enum class ELLRemapPreset : uint8
{
	None    UMETA(DisplayName	= "None / Manual"),
	ARKit   UMETA(DisplayName	= "ARKit (MetaHuman-friendly)"),
	VMC_VRM UMETA(DisplayName	= "VMC / VRM (VMC protocol-style)"),
	VRoid	UMETA(DisplayName	= "VMC / VRoid"),
	Rokoko  UMETA(DisplayName	= "Rokoko (ARKit names)"),
	Custom  UMETA(DisplayName	= "Custom (JSON)")
};


// ---------------- Worker ----------------
class FVMCLiveLinkRemapperWorker final : public ILiveLinkSubjectRemapperWorker
{
public:
	// Value shaping toggles (copied from asset on CreateWorker)
	bool  bEnableMetaHumanCurveNormalizer = true;
	float JoyToSmileStrength = 1.0f;
	float BlinkMirrorStrength = 1.0f;

	virtual void RemapStaticData(FLiveLinkStaticDataStruct& InOutStaticData) override
	{
		if (!InOutStaticData.IsValid() ||
			!InOutStaticData.GetStruct()->IsChildOf<FLiveLinkSkeletonStaticData>()) return;

		auto& Skel = *InOutStaticData.Cast<FLiveLinkSkeletonStaticData>();

		// Bones (use accessors for safety)
		TArray<FName> Remapped = Skel.GetBoneNames();
		for (FName& N : Remapped)
		{
			if (const FName* Out = BoneNameMap.Find(N)) N = *Out;
		}
		Skel.SetBoneNames(Remapped);

		// Curves live on base static data in 5.6
		FLiveLinkBaseStaticData& Base = static_cast<FLiveLinkBaseStaticData&>(Skel);
		for (FName& C : Base.PropertyNames)
		{
			if (const FName* Out = CurveNameMap.Find(C)) C = *Out;
		}
	}

	virtual void RemapFrameData(const FLiveLinkStaticDataStruct& InStatic, FLiveLinkFrameDataStruct& InOutFrameData) override
	{
		if (!InStatic.IsValid() || !InOutFrameData.IsValid()) return;
		if (!InStatic.GetStruct()->IsChildOf<FLiveLinkSkeletonStaticData>() ||
			!InOutFrameData.GetStruct()->IsChildOf<FLiveLinkAnimationFrameData>()) return;

		const FLiveLinkBaseStaticData& Base = *InStatic.Cast<FLiveLinkBaseStaticData>();
		const TArray<FName>& Names = Base.PropertyNames;


		auto& Anim = *InOutFrameData.Cast<FLiveLinkAnimationFrameData>();
		TArray<float>& Values = Anim.PropertyValues;

		if (!bEnableMetaHumanCurveNormalizer) return;

		auto FindIdx = [&](FName Name)->int32 { return Names.IndexOfByKey(Name); };
		auto Get = [&](FName Name, float& Out)->bool {
			const int32 I = FindIdx(Name);
			if (I != INDEX_NONE && Values.IsValidIndex(I)) { Out = Values[I]; return true; }
			return false;
			};
		auto Set = [&](FName Name, float Val)->void {
			const int32 I = FindIdx(Name);
			if (I != INDEX_NONE && Values.IsValidIndex(I)) { Values[I] = Val; }
			};

		// Blink mirroring
		float BlinkL = 0.f, BlinkR = 0.f;
		const bool HasL = Get("eyeBlinkLeft", BlinkL);
		const bool HasR = Get("eyeBlinkRight", BlinkR);
		if (HasL && !HasR) Set("eyeBlinkRight", FMath::Clamp(BlinkL * BlinkMirrorStrength, 0.f, 1.f));
		if (HasR && !HasL) Set("eyeBlinkLeft", FMath::Clamp(BlinkR * BlinkMirrorStrength, 0.f, 1.f));

		// Smile spreading
		float Joy = 0.f;
		if (Get("mouthSmileLeft", Joy))
		{
			const float V = FMath::Clamp(Joy * JoyToSmileStrength, 0.f, 1.f);
			Set("mouthSmileLeft", V);
			Set("mouthSmileRight", V);
		}

		// Funnel→pucker blend
		float Funnel = 0.f;
		if (Get("mouthFunnel", Funnel))
		{
			Set("mouthPucker", FMath::Clamp(Funnel * 0.5f, 0.f, 1.f));
		}
	}

	TMap<FName, FName> BoneNameMap;   // copied from asset on CreateWorker
	TMap<FName, FName> CurveNameMap;  // copied from asset on CreateWorker
};

// ---------------- Asset ----------------
UCLASS(MinimalAPI, EditInlineNew, DefaultToInstanced)
class UVMCLiveLinkRemapper final : public ULiveLinkSubjectRemapper
{
	GENERATED_BODY()
public:
	// Remapper API
	virtual void Initialize(const FLiveLinkSubjectKey& InSubjectKey) override;
	virtual TSubclassOf<ULiveLinkRole> GetSupportedRole() const override { return ULiveLinkAnimationRole::StaticClass(); }
	virtual bool IsValidRemapper() const override { return true; }
	virtual FWorkerSharedPtr GetWorker() const override { return Worker; }
	virtual FWorkerSharedPtr CreateWorker() override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& Evt) override
	{
		Super::PostEditChangeProperty(Evt);
		bDirty = true;       // <-- important
		SyncWorker();
		RequestStaticDataRefresh();
	}
#endif

	// Utilities
	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void ForceRefreshStaticData() { RequestStaticDataRefresh(); }

	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void DetectAndSeedFromSubject();

	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void ApplyPreset(ELLRemapPreset InPreset);

	UFUNCTION(BlueprintCallable, Category = "LiveLink|Remapper")
	void LoadCustomCurveMapFromJSON(const FString& JsonText);

public:
	// NOTE: BoneNameMap is declared on the base (ULiveLinkSubjectRemapper). Don't redeclare it here.

	UPROPERTY(EditAnywhere, Category = "Remapper")
	TMap<FName, FName> CurveNameMap;

	UPROPERTY(EditAnywhere, Category = "Remapper|Skeleton", meta = (DisplayThumbnail = "false"))
	TSoftObjectPtr<USkeletalMesh> ReferenceSkeleton;

	UPROPERTY(EditAnywhere, Category = "Remapper|Preset")
	ELLRemapPreset Preset = ELLRemapPreset::None;

	// MetaHuman curve value shaping (optional)
	UPROPERTY(EditAnywhere, Category = "Normalizer")
	bool bEnableMetaHumanCurveNormalizer = true;

	UPROPERTY(EditAnywhere, Category = "Normalizer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float JoyToSmileStrength = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Normalizer", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BlinkMirrorStrength = 1.0f;

private:
	// Helpers
	void RequestStaticDataRefresh();   // flips bDirty
	void SyncWorker() const;

	void SeedFromReferenceSkeleton();
	void SeedCurves_ARKit();
	void SeedCurves_VMC_VRM();
	void SeedCurvesAndBones_VRoid();
	void SeedCurves_Rokoko();
	void SeedBones_FromHumanoidLike(const TArray<FName>& Incoming);

	ELLRemapPreset GuessPreset(const TArray<FName>& BoneNames, const TArray<FName>& CurveNames) const;

private:
	FLiveLinkSubjectKey CachedKey;
	mutable TSharedPtr<FVMCLiveLinkRemapperWorker> Worker;
};
