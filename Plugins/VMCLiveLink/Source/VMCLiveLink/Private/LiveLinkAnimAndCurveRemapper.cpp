// LiveLinkAnimAndCurveRemapper.cpp - UE 5.6+

#include "LiveLinkAnimAndCurveRemapper.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "RemapSync.h"

// Qualify the return type to avoid “assumed int / different basic type”.
ULiveLinkSubjectRemapper::FWorkerSharedPtr ULiveLinkAnimAndCurveRemapper::CreateWorker()
{
	Worker = MakeShared<FLiveLinkAnimAndCurveRemapperWorker>();
	Worker->BoneNameMap = BoneNameMap;     // base class map
	Worker->CurveNameMap = CurveNameMap;    // our curve map

	Worker->bEnableMetaHumanCurveNormalizer = bEnableMetaHumanCurveNormalizer;
	Worker->JoyToSmileStrength = JoyToSmileStrength;
	Worker->BlinkMirrorStrength = BlinkMirrorStrength;
	return Worker;
}

void ULiveLinkAnimAndCurveRemapper::Initialize(const FLiveLinkSubjectKey& InSubjectKey)
{
	CachedKey = InSubjectKey;

	// Seed identity maps + guess a preset from current subject static data
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient& Client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		if (const FLiveLinkStaticDataStruct* SDS = Client.GetSubjectStaticData_AnyThread(InSubjectKey))
		{
			if (SDS->IsValid() && SDS->GetStruct()->IsChildOf<FLiveLinkSkeletonStaticData>())
			{
				const auto& Skel = *SDS->Cast<FLiveLinkSkeletonStaticData>();
				const FLiveLinkBaseStaticData& Base = static_cast<const FLiveLinkBaseStaticData&>(Skel);

				if (BoneNameMap.Num() == 0)  for (const FName& N : Skel.GetBoneNames())     BoneNameMap.Add(N, N);
				if (CurveNameMap.Num() == 0) for (const FName& N : Base.PropertyNames)      CurveNameMap.Add(N, N);

				Preset = GuessPreset(Skel.GetBoneNames(), Base.PropertyNames);
				ApplyPreset(Preset);
			}
		}
	}

	SeedFromReferenceSkeleton();
	SyncWorker();
	RequestStaticDataRefresh(); // make it take effect now
}

void ULiveLinkAnimAndCurveRemapper::RequestStaticDataRefresh()
{
	bDirty = true; // <-- force the remapper to rebuild mappings now
	SyncWorker();
}


void ULiveLinkAnimAndCurveRemapper::SyncWorker() const
{
	if (!Worker.IsValid()) return;
	Worker->BoneNameMap = BoneNameMap;
	Worker->CurveNameMap = CurveNameMap;

	Worker->bEnableMetaHumanCurveNormalizer = bEnableMetaHumanCurveNormalizer;
	Worker->JoyToSmileStrength = JoyToSmileStrength;
	Worker->BlinkMirrorStrength = BlinkMirrorStrength;
}

void ULiveLinkAnimAndCurveRemapper::DetectAndSeedFromSubject()
{
	if (!IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName)) return;

	ILiveLinkClient& Client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
	if (const FLiveLinkStaticDataStruct* SDS = Client.GetSubjectStaticData_AnyThread(CachedKey))
	{
		if (SDS->IsValid() && SDS->GetStruct()->IsChildOf<FLiveLinkSkeletonStaticData>())
		{
			const auto& Skel = *SDS->Cast<FLiveLinkSkeletonStaticData>();
			const FLiveLinkBaseStaticData& Base = static_cast<const FLiveLinkBaseStaticData&>(Skel);
			Preset = GuessPreset(Skel.GetBoneNames(), Base.PropertyNames);
			ApplyPreset(Preset);
		}
	}
}

void ULiveLinkAnimAndCurveRemapper::ApplyPreset(ELLRemapPreset InPreset)
{
	switch (InPreset)
	{
	case ELLRemapPreset::ARKit:   SeedCurves_ARKit();   break;
	case ELLRemapPreset::VMC_VRM: SeedCurves_VMC_VRM(); break;
	case ELLRemapPreset::Rokoko:  SeedCurves_Rokoko();  break;
	default: break;
	}

	// If we have subject data, nudge humanoid bone names toward the reference mesh
	if (IModularFeatures::Get().IsModularFeatureAvailable(ILiveLinkClient::ModularFeatureName))
	{
		ILiveLinkClient& Client = IModularFeatures::Get().GetModularFeature<ILiveLinkClient>(ILiveLinkClient::ModularFeatureName);
		if (const FLiveLinkStaticDataStruct* SDS = Client.GetSubjectStaticData_AnyThread(CachedKey))
		{
			if (SDS->IsValid() && SDS->GetStruct()->IsChildOf<FLiveLinkSkeletonStaticData>())
			{
				SeedBones_FromHumanoidLike(SDS->Cast<FLiveLinkSkeletonStaticData>()->GetBoneNames());
			}
		}
	}

	SyncWorker();
	RequestStaticDataRefresh();
}

void ULiveLinkAnimAndCurveRemapper::LoadCustomCurveMapFromJSON(const FString& JsonText)
{
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
	if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
	{
		if (const TSharedPtr<FJsonObject>* CurvesObj; Root->TryGetObjectField(TEXT("Curves"), CurvesObj))
		{
			for (const auto& KV : (*CurvesObj)->Values)
			{
				if (KV.Value.IsValid() && KV.Value->Type == EJson::String)
				{
					CurveNameMap.Add(FName(*KV.Key), FName(*KV.Value->AsString()));
				}
			}
		}
		if (const TSharedPtr<FJsonObject>* BonesObj; Root->TryGetObjectField(TEXT("Bones"), BonesObj))
		{
			for (const auto& KV : (*BonesObj)->Values)
			{
				if (KV.Value.IsValid() && KV.Value->Type == EJson::String)
				{
					BoneNameMap.Add(FName(*KV.Key), FName(*KV.Value->AsString()));
				}
			}
		}
	}
	SyncWorker();
	RequestStaticDataRefresh();
}

// -------------- Seeding --------------

void ULiveLinkAnimAndCurveRemapper::SeedCurves_ARKit()
{
	static const TCHAR* ARKit[] = {
		TEXT("browDownLeft"), TEXT("browDownRight"), TEXT("browInnerUp"),
		TEXT("browOuterUpLeft"), TEXT("browOuterUpRight"),
		TEXT("cheekPuff"), TEXT("cheekSquintLeft"), TEXT("cheekSquintRight"),
		TEXT("eyeBlinkLeft"), TEXT("eyeBlinkRight"),
		TEXT("eyeLookDownLeft"), TEXT("eyeLookDownRight"),
		TEXT("eyeLookInLeft"), TEXT("eyeLookInRight"),
		TEXT("eyeLookOutLeft"), TEXT("eyeLookOutRight"),
		TEXT("eyeLookUpLeft"), TEXT("eyeLookUpRight"),
		TEXT("eyeSquintLeft"), TEXT("eyeSquintRight"),
		TEXT("eyeWideLeft"), TEXT("eyeWideRight"),
		TEXT("jawForward"), TEXT("jawLeft"), TEXT("jawOpen"), TEXT("jawRight"),
		TEXT("mouthClose"), TEXT("mouthDimpleLeft"), TEXT("mouthDimpleRight"),
		TEXT("mouthFrownLeft"), TEXT("mouthFrownRight"),
		TEXT("mouthFunnel"), TEXT("mouthLeft"), TEXT("mouthLowerDownLeft"),
		TEXT("mouthLowerDownRight"), TEXT("mouthPressLeft"), TEXT("mouthPressRight"),
		TEXT("mouthPucker"), TEXT("mouthRight"), TEXT("mouthRollLower"),
		TEXT("mouthRollUpper"), TEXT("mouthShrugLower"), TEXT("mouthShrugUpper"),
		TEXT("mouthSmileLeft"), TEXT("mouthSmileRight"),
		TEXT("mouthStretchLeft"), TEXT("mouthStretchRight"),
		TEXT("mouthUpperUpLeft"), TEXT("mouthUpperUpRight"),
		TEXT("noseSneerLeft"), TEXT("noseSneerRight"),
		TEXT("tongueOut")
	};
	for (const TCHAR* Name : ARKit)
	{
		CurveNameMap.FindOrAdd(FName(Name)) = FName(Name);
	}
}

void ULiveLinkAnimAndCurveRemapper::SeedCurves_VMC_VRM()
{
	// Common VMC/VRM → ARKit-ish targets. Expand to match your source.
	CurveNameMap.FindOrAdd("Blink") = "eyeBlinkLeft"; // single blink → we’ll mirror in runtime
	CurveNameMap.FindOrAdd("Blink_L") = "eyeBlinkLeft";
	CurveNameMap.FindOrAdd("Blink_R") = "eyeBlinkRight";

	CurveNameMap.FindOrAdd("Joy") = "mouthSmileLeft";
	CurveNameMap.FindOrAdd("Angry") = "browDownLeft";
	CurveNameMap.FindOrAdd("Sorrow") = "mouthFrownLeft";
	CurveNameMap.FindOrAdd("Fun") = "cheekPuff";

	// A I U E O → a pragmatic ARKit set
	CurveNameMap.FindOrAdd("A") = "jawOpen";
	CurveNameMap.FindOrAdd("I") = "mouthSmileLeft";
	CurveNameMap.FindOrAdd("U") = "mouthPucker";
	CurveNameMap.FindOrAdd("E") = "mouthStretchLeft";
	CurveNameMap.FindOrAdd("O") = "mouthFunnel";

	// Brows
	CurveNameMap.FindOrAdd("BrowDownLeft") = "browDownLeft";
	CurveNameMap.FindOrAdd("BrowDownRight") = "browDownRight";
	CurveNameMap.FindOrAdd("BrowUpLeft") = "browOuterUpLeft";
	CurveNameMap.FindOrAdd("BrowUpRight") = "browOuterUpRight";
}

void ULiveLinkAnimAndCurveRemapper::SeedCurves_Rokoko()
{
	SeedCurves_ARKit(); // Rokoko typically forwards ARKit names
	// Common alias fixes
	CurveNameMap.FindOrAdd("mouthSmile_L") = "mouthSmileLeft";
	CurveNameMap.FindOrAdd("mouthSmile_R") = "mouthSmileRight";
}

void ULiveLinkAnimAndCurveRemapper::SeedBones_FromHumanoidLike(const TArray<FName>& Incoming)
{
	USkeletalMesh* Ref = ReferenceSkeleton.LoadSynchronous();
	if (!Ref) return;

	auto Normalize = [](FString S)
		{
			S = S.ToLower();
			S.ReplaceInline(TEXT("_"), TEXT(""));
			S.ReplaceInline(TEXT("-"), TEXT(""));
			return S;
		};

	const FReferenceSkeleton& RefSkel = Ref->GetRefSkeleton();
	TMap<FString, FName> RefByNorm;
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
	{
		const FName B = RefSkel.GetBoneName(i);
		RefByNorm.Add(Normalize(B.ToString()), B);
	}

	auto TryMap = [&](FName Src, const TArray<FString>& Candidates)
		{
			for (const FString& C : Candidates)
			{
				if (const FName* Found = RefByNorm.Find(Normalize(C)))
				{
					BoneNameMap.Add(Src, *Found);
					return true;
				}
			}
			return false;
		};

	for (FName Src : Incoming)
	{
		const FString N = Src.ToString();

		if (N.Equals(TEXT("Hips"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("pelvis") }); continue; }
		if (N.Equals(TEXT("Spine"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("spine_01"), TEXT("spine01"), TEXT("spine") }); continue; }
		if (N.Equals(TEXT("Chest"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("spine_02"), TEXT("spine02") }); continue; }
		if (N.Equals(TEXT("UpperChest"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("spine_03"), TEXT("spine03") }); continue; }
		if (N.Equals(TEXT("Neck"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("neck_01"), TEXT("neck") }); continue; }
		if (N.Equals(TEXT("Head"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("head") }); continue; }

		// Arms
		if (N.Contains(TEXT("LeftUpperArm"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("upperarm_l") }); continue; }
		if (N.Contains(TEXT("LeftLowerArm"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("lowerarm_l"), TEXT("forearm_l") }); continue; }
		if (N.Contains(TEXT("LeftHand"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("hand_l") }); continue; }

		if (N.Contains(TEXT("RightUpperArm"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("upperarm_r") }); continue; }
		if (N.Contains(TEXT("RightLowerArm"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("lowerarm_r"), TEXT("forearm_r") }); continue; }
		if (N.Contains(TEXT("RightHand"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("hand_r") }); continue; }

		// Legs
		if (N.Contains(TEXT("LeftUpperLeg"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("thigh_l") }); continue; }
		if (N.Contains(TEXT("LeftLowerLeg"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("calf_l") }); continue; }
		if (N.Contains(TEXT("LeftFoot"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("foot_l") }); continue; }

		if (N.Contains(TEXT("RightUpperLeg"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("thigh_r") }); continue; }
		if (N.Contains(TEXT("RightLowerLeg"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("calf_r") }); continue; }
		if (N.Contains(TEXT("RightFoot"), ESearchCase::IgnoreCase)) { TryMap(Src, { TEXT("foot_r") }); continue; }
	}
}

void ULiveLinkAnimAndCurveRemapper::SeedFromReferenceSkeleton()
{
	// Add project-specific bone aliases here if you want.
}

ELLRemapPreset ULiveLinkAnimAndCurveRemapper::GuessPreset(const TArray<FName>& /*BoneNames*/, const TArray<FName>& CurveNames) const
{
	int32 ARKitHits = 0;
	for (const FName& N : CurveNames)
	{
		const FString S = N.ToString();
		if (S.StartsWith(TEXT("eye")) || S.StartsWith(TEXT("mouth")) || S.StartsWith(TEXT("brow")) || S == TEXT("tongueOut") || S.StartsWith(TEXT("jaw")))
			++ARKitHits;
	}
	if (ARKitHits >= 20) return ELLRemapPreset::ARKit;

	bool HasVisemes = false, HasBlinkLR = false, HasEmotes = false;
	for (const FName& N : CurveNames)
	{
		const FString S = N.ToString();
		if (S == TEXT("A") || S == TEXT("I") || S == TEXT("U") || S == TEXT("E") || S == TEXT("O")) HasVisemes = true;
		if (S == TEXT("Blink_L") || S == TEXT("Blink_R")) HasBlinkLR = true;
		if (S == TEXT("Joy") || S == TEXT("Angry") || S == TEXT("Sorrow") || S == TEXT("Fun")) HasEmotes = true;
	}
	if ((HasVisemes && HasBlinkLR) || (HasVisemes && HasEmotes)) return ELLRemapPreset::VMC_VRM;

	return ELLRemapPreset::None;
}

