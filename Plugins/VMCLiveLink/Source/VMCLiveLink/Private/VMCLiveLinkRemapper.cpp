// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. / Athomas Goldberg. All Rights Reserved.
﻿// LiveLinkAnimAndCurveRemapper.cpp - UE 5.6+

#include "VMCLiveLinkRemapper.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include <Remapper/LiveLinkSkeletonRemapper.h>
#include <LiveLinkRemapAsset.h>


static void GetBoneNames(TSoftObjectPtr<USkeletalMesh> Mesh, TArray<FName>& Out)
{
	Out.Reset();
	if (!Mesh) return;

	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
	const int32 NumBones = RefSkel.GetNum();
	Out.Reserve(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		Out.Add(RefSkel.GetBoneName(i));
	}
}

// Qualify the return type to avoid “assumed int / different basic type”.
ULiveLinkSubjectRemapper::FWorkerSharedPtr UVMCLiveLinkRemapper::CreateWorker()
{
	Worker = MakeShared<FVMCLiveLinkRemapperWorker>();
	Worker->BoneNameMap = BoneNameMap;     // base class map
	Worker->CurveNameMap = CurveNameMap;    // our curve map
	Worker->bEnableMetaHumanCurveNormalizer = bEnableMetaHumanCurveNormalizer;
	Worker->JoyToSmileStrength = JoyToSmileStrength;
	Worker->BlinkMirrorStrength = BlinkMirrorStrength;
	return Worker;
}

void UVMCLiveLinkRemapper::Initialize(const FLiveLinkSubjectKey& InSubjectKey)
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

void UVMCLiveLinkRemapper::RequestStaticDataRefresh()
{
	bDirty = true; // <-- force the remapper to rebuild mappings now
	SyncWorker();
}


void UVMCLiveLinkRemapper::SyncWorker() const
{
	if (!Worker.IsValid()) return;
	Worker->BoneNameMap = BoneNameMap;
	Worker->CurveNameMap = CurveNameMap;

	Worker->bEnableMetaHumanCurveNormalizer = bEnableMetaHumanCurveNormalizer;
	Worker->JoyToSmileStrength = JoyToSmileStrength;
	Worker->BlinkMirrorStrength = BlinkMirrorStrength;
}

void UVMCLiveLinkRemapper::DetectAndSeedFromSubject()
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

void UVMCLiveLinkRemapper::ApplyPreset(ELLRemapPreset InPreset)
{
	switch (InPreset)
	{
	case ELLRemapPreset::ARKit:		SeedCurves_ARKit();   break;
	case ELLRemapPreset::VMC_VRM:	SeedCurves_VMC_VRM(); break;
	case ELLRemapPreset::VRoid:		SeedCurvesAndBones_VRoid(); break;
	case ELLRemapPreset::Rokoko:	SeedCurves_Rokoko();  break;
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

void UVMCLiveLinkRemapper::LoadCustomCurveMapFromJSON(const FString& JsonText)
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

void UVMCLiveLinkRemapper::SeedCurves_ARKit()
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

void UVMCLiveLinkRemapper::SeedCurvesAndBones_VRoid()
{
	BoneNameMap.FindOrAdd("Hips")			= "J_Bip_C_Hips";
	BoneNameMap.FindOrAdd("Spine")			= "J_Bip_C_Spine";
	BoneNameMap.FindOrAdd("Chest")			= "J_Bip_C_Chest";
	BoneNameMap.FindOrAdd("UpperChest")		= "J_Bip_C_UpperChest";
	BoneNameMap.FindOrAdd("Neck")			= "J_Bip_C_Neck";
	BoneNameMap.FindOrAdd("Head")			= "J_Bip_C_Head";
	BoneNameMap.FindOrAdd("LeftEye")		= "J_Adj_L_FaceEye";
	BoneNameMap.FindOrAdd("RightEye")		= "J_Adj_R_FaceEye";
	BoneNameMap.FindOrAdd("LeftUpperLeg")	= "J_Bip_L_UpperLeg";
	BoneNameMap.FindOrAdd("RightUpperLeg")	= "J_Bip_R_UpperLeg";
	BoneNameMap.FindOrAdd("LeftLowerLeg")	= "J_Bip_L_LowerLeg";
	BoneNameMap.FindOrAdd("RightLowerLeg")	= "J_Bip_R_LowerLeg";
	BoneNameMap.FindOrAdd("LeftFoot")		= "J_Bip_L_Foot";
	BoneNameMap.FindOrAdd("RightFoot")		= "J_Bip_R_Foot";
	BoneNameMap.FindOrAdd("LeftToes")		= "J_Bip_L_Toes";
	BoneNameMap.FindOrAdd("RightToes")		= "J_Bip_R_Toes";
	BoneNameMap.FindOrAdd("LeftShoulder")	= "J_Bip_L_Shoulder";
	BoneNameMap.FindOrAdd("RightShoulder")	= "J_Bip_R_Shoulder";
	BoneNameMap.FindOrAdd("LeftUpperArm")	= "J_Bip_L_UpperArm";
	BoneNameMap.FindOrAdd("RightUpperArm")	= "J_Bip_R_UpperArm";
	BoneNameMap.FindOrAdd("LeftLowerArm")	= "J_Bip_L_LowerArm";
	BoneNameMap.FindOrAdd("RightLowerArm")	= "J_Bip_R_LowerArm";
	BoneNameMap.FindOrAdd("LeftHand")		= "J_Bip_L_Hand";
	BoneNameMap.FindOrAdd("RightHand")		= "J_Bip_R_Hand";
	BoneNameMap.FindOrAdd("LeftThumbProximal")			= "J_Bip_L_Thumb1";
	BoneNameMap.FindOrAdd("LeftThumbIntermediate")		= "J_Bip_L_Thumb2";
	BoneNameMap.FindOrAdd("LeftThumbDistal")			= "J_Bip_L_Thumb3";
	BoneNameMap.FindOrAdd("RightThumbProximal")			= "J_Bip_R_Thumb1";
	BoneNameMap.FindOrAdd("RightThumbIntermediate")		= "J_Bip_R_Thumb2";
	BoneNameMap.FindOrAdd("RightThumbDistal")			= "J_Bip_R_Thumb3";

	BoneNameMap.FindOrAdd("LeftIndexProximal")			= "J_Bip_L_Index1";
	BoneNameMap.FindOrAdd("LeftIndexIntermediate")		= "J_Bip_L_Index2";
	BoneNameMap.FindOrAdd("LeftIndexDistal")			= "J_Bip_L_Index3";
	BoneNameMap.FindOrAdd("RightIndexProximal")			= "J_Bip_R_Index1";
	BoneNameMap.FindOrAdd("RightIndexIntermediate")		= "J_Bip_R_Index2";
	BoneNameMap.FindOrAdd("RightIndexDistal")			= "J_Bip_R_Index3";

	BoneNameMap.FindOrAdd("LeftMiddleProximal")			= "J_Bip_L_Middle1";
	BoneNameMap.FindOrAdd("LeftMiddleIntermediate")		= "J_Bip_L_Middle2";
	BoneNameMap.FindOrAdd("LeftMiddleDistal")			= "J_Bip_L_Middle3";
	BoneNameMap.FindOrAdd("RightMiddleProximal")		= "J_Bip_R_Middle1";
	BoneNameMap.FindOrAdd("RightMiddleIntermediate")	= "J_Bip_R_Middle2";
	BoneNameMap.FindOrAdd("RightMiddleDistal")			= "J_Bip_R_Middle3";

	BoneNameMap.FindOrAdd("LeftRingProximal")			= "J_Bip_L_Ring1";
	BoneNameMap.FindOrAdd("LeftRingIntermediate")		= "J_Bip_L_Ring2";
	BoneNameMap.FindOrAdd("LeftRingDistal")				= "J_Bip_L_Ring3";
	BoneNameMap.FindOrAdd("RightRingProximal")			= "J_Bip_R_Ring1";
	BoneNameMap.FindOrAdd("RightRingIntermediate")		= "J_Bip_R_Ring2";
	BoneNameMap.FindOrAdd("RightRingDistal")			= "J_Bip_R_Ring3";

	BoneNameMap.FindOrAdd("LeftLittleProximal")			= "J_Bip_L_Little1";
	BoneNameMap.FindOrAdd("LeftLittleIntermediate")		= "J_Bip_L_Little2";
	BoneNameMap.FindOrAdd("LeftLittleDistal")			= "J_Bip_L_Little3";
	BoneNameMap.FindOrAdd("RightLittleProximal")		= "J_Bip_R_Little1";
	BoneNameMap.FindOrAdd("RightLittleIntermediate")	= "J_Bip_R_Little2";
	BoneNameMap.FindOrAdd("RightLittleDistal")			= "J_Bip_R_Little3";

	CurveNameMap.FindOrAdd("Blink") = "Face.M_F00_000_Fcl_EYE_Close"; // single blink → we’ll mirror in runtime
	CurveNameMap.FindOrAdd("Blink_L") = "Face.M_F00_000_Fcl_EYE_Close_L";
	CurveNameMap.FindOrAdd("Blink_R") = "Face.M_F00_000_Fcl_EYE_Close_R";

	CurveNameMap.FindOrAdd("Joy") = "Face.M_F00_000_Fcl_ALL_Joy";
	CurveNameMap.FindOrAdd("Angry") = "Face.M_F00_000_Fcl_ALL_Angry";
	CurveNameMap.FindOrAdd("Sorrow") = "Face.M_F00_000_Fcl_ALL_Sorrow";
	CurveNameMap.FindOrAdd("Fun") = "Face.M_F00_000_Fcl_ALL_Fun";

	// A I U E O → a pragmatic ARKit set
	CurveNameMap.FindOrAdd("A") = "Face.M_F00_000_Fcl_MTH_A";
	CurveNameMap.FindOrAdd("I") = "Face.M_F00_000_Fcl_MTH_I";
	CurveNameMap.FindOrAdd("U") = "Face.M_F00_000_Fcl_MTH_U";
	CurveNameMap.FindOrAdd("E") = "Face.M_F00_000_Fcl_MTH_E";
	CurveNameMap.FindOrAdd("O") = "Face.M_F00_000_Fcl_MTH_O";
}

void UVMCLiveLinkRemapper::SeedCurves_VMC_VRM()
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

	//	BoneNameMap.FindOrAdd("Hips")			= "J_Bip_C_Hips";
	//	BoneNameMap.FindOrAdd("Spine")			= "J_Bip_C_Spine";
	//	BoneNameMap.FindOrAdd("Chest")			= "J_Bip_C_Chest";
	//	BoneNameMap.FindOrAdd("UpperChest")		= "J_Bip_C_UpperChest";
	//	BoneNameMap.FindOrAdd("Neck")			= "J_Bip_C_Neck";
	//	BoneNameMap.FindOrAdd("Head")			= "J_Bip_C_Head";


}



void UVMCLiveLinkRemapper::SeedCurves_Rokoko()
{
	SeedCurves_ARKit(); // Rokoko typically forwards ARKit names
	// Common alias fixes
	CurveNameMap.FindOrAdd("mouthSmile_L") = "mouthSmileLeft";
	CurveNameMap.FindOrAdd("mouthSmile_R") = "mouthSmileRight";
}

void UVMCLiveLinkRemapper::SeedBones_FromHumanoidLike(const TArray<FName>& Incoming)
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

void UVMCLiveLinkRemapper::SeedFromReferenceSkeleton()
{



}

ELLRemapPreset UVMCLiveLinkRemapper::GuessPreset(const TArray<FName>& BoneNames, const TArray<FName>& CurveNames) const
{
	
	TArray<FName> RefSkelBoneNames;
	GetBoneNames(ReferenceSkeleton, RefSkelBoneNames);

	for (const FName& N : RefSkelBoneNames)
	{
		const FString S = N.ToString();
		if (S.StartsWith(TEXT("J_Bip_"))) return ELLRemapPreset::VRoid;
	}

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

