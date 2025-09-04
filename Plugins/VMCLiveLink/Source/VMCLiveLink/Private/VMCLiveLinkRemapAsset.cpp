#include "VMCLiveLinkRemapAsset.h"
#include "UObject/Package.h" // MarkPackageDirty

// ---------------- core remap hooks ----------------

FName UVMCLiveLinkRemapAsset::GetRemappedBoneName_Implementation(FName BoneName) const
{
	EnsureLoaded();
	if (const FName* Out = BoneNameMap.Find(BoneName))
	{
		return *Out;
	}
	return BoneName;
}

FName UVMCLiveLinkRemapAsset::GetRemappedCurveName_Implementation(FName CurveName) const
{
	EnsureLoaded();
	if (const FName* Out = CurveNameMap.Find(CurveName))
	{
		return *Out;
	}
	return CurveName;
}

// ---------------- data loading ----------------

void UVMCLiveLinkRemapAsset::EnsureLoaded() const
{
	if (!bLoadedFromTable && RemapTable)
	{
		// const_cast to call non-const loader inside const context of remap functions
		const_cast<UVMCLiveLinkRemapAsset*>(this)->LoadFromTable();
	}
}

void UVMCLiveLinkRemapAsset::LoadFromTable()
{
	if (!RemapTable)
	{
		bLoadedFromTable = true; // nothing to do
		return;
	}

	BoneNameMap.Empty();
	CurveNameMap.Empty();

	static const FString Context(TEXT("VMCLiveLinkRemapAsset_Load"));
	TArray<FVMCRemapRow*> Rows;
	RemapTable->GetAllRows(Context, Rows);

	for (const FVMCRemapRow* Row : Rows)
	{
		if (!Row) continue;

		const FName Src = Row->Source;
		const FName Dst = Row->Target;
		if (Src.IsNone() || Dst.IsNone()) continue;

		if (Row->Type == EVMCRemapType::Bone)
		{
			BoneNameMap.Add(Src, Dst);
		}
		else
		{
			CurveNameMap.Add(Src, Dst);
		}
	}

	bLoadedFromTable = true;
}

void UVMCLiveLinkRemapAsset::ReloadFromTable()
{
	Modify();
	bLoadedFromTable = false;
	LoadFromTable();
	MarkPackageDirty();
}

// ---------------- presets (manual) ----------------

static void AddFingerTriplet(TMap<FName, FName>& M,
	const TCHAR* VmcProx, const TCHAR* VmcInter, const TCHAR* VmcDist,
	const TCHAR* Ue01, const TCHAR* Ue02, const TCHAR* Ue03)
{
	M.Add(FName(VmcProx), FName(Ue01));
	M.Add(FName(VmcInter), FName(Ue02));
	M.Add(FName(VmcDist), FName(Ue03));
}

void UVMCLiveLinkRemapAsset::SeedFingerMap_UE()
{
	Modify();
	BoneNameMap.Empty();

	// LEFT
	AddFingerTriplet(BoneNameMap,
		TEXT("leftThumbProximal"), TEXT("leftThumbIntermediate"), TEXT("leftThumbDistal"),
		TEXT("thumb_01_l"), TEXT("thumb_02_l"), TEXT("thumb_03_l"));
	AddFingerTriplet(BoneNameMap,
		TEXT("leftIndexProximal"), TEXT("leftIndexIntermediate"), TEXT("leftIndexDistal"),
		TEXT("index_01_l"), TEXT("index_02_l"), TEXT("index_03_l"));
	AddFingerTriplet(BoneNameMap,
		TEXT("leftMiddleProximal"), TEXT("leftMiddleIntermediate"), TEXT("leftMiddleDistal"),
		TEXT("middle_01_l"), TEXT("middle_02_l"), TEXT("middle_03_l"));
	AddFingerTriplet(BoneNameMap,
		TEXT("leftRingProximal"), TEXT("leftRingIntermediate"), TEXT("leftRingDistal"),
		TEXT("ring_01_l"), TEXT("ring_02_l"), TEXT("ring_03_l"));
	AddFingerTriplet(BoneNameMap,
		TEXT("leftLittleProximal"), TEXT("leftLittleIntermediate"), TEXT("leftLittleDistal"),
		TEXT("pinky_01_l"), TEXT("pinky_02_l"), TEXT("pinky_03_l"));

	// RIGHT
	AddFingerTriplet(BoneNameMap,
		TEXT("rightThumbProximal"), TEXT("rightThumbIntermediate"), TEXT("rightThumbDistal"),
		TEXT("thumb_01_r"), TEXT("thumb_02_r"), TEXT("thumb_03_r"));
	AddFingerTriplet(BoneNameMap,
		TEXT("rightIndexProximal"), TEXT("rightIndexIntermediate"), TEXT("rightIndexDistal"),
		TEXT("index_01_r"), TEXT("index_02_r"), TEXT("index_03_r"));
	AddFingerTriplet(BoneNameMap,
		TEXT("rightMiddleProximal"), TEXT("rightMiddleIntermediate"), TEXT("rightMiddleDistal"),
		TEXT("middle_01_r"), TEXT("middle_02_r"), TEXT("middle_03_r"));
	AddFingerTriplet(BoneNameMap,
		TEXT("rightRingProximal"), TEXT("rightRingIntermediate"), TEXT("rightRingDistal"),
		TEXT("ring_01_r"), TEXT("ring_02_r"), TEXT("ring_03_r"));
	AddFingerTriplet(BoneNameMap,
		TEXT("rightLittleProximal"), TEXT("rightLittleIntermediate"), TEXT("rightLittleDistal"),
		TEXT("pinky_01_r"), TEXT("pinky_02_r"), TEXT("pinky_03_r"));

	BoneNameMap.Add(TEXT("leftHand"), TEXT("hand_l"));
	BoneNameMap.Add(TEXT("rightHand"), TEXT("hand_r"));

	MarkPackageDirty();
}

void UVMCLiveLinkRemapAsset::SeedCurveMap_Common()
{
	Modify();
	CurveNameMap.Empty();

	CurveNameMap.Add(TEXT("JawOpen"), TEXT("jawOpen"));
	CurveNameMap.Add(TEXT("EyeBlinkLeft"), TEXT("eyeBlinkLeft"));
	CurveNameMap.Add(TEXT("EyeBlinkRight"), TEXT("eyeBlinkRight"));

	CurveNameMap.Add(TEXT("A"), TEXT("mouthA"));
	CurveNameMap.Add(TEXT("I"), TEXT("mouthI"));
	CurveNameMap.Add(TEXT("U"), TEXT("mouthU"));
	CurveNameMap.Add(TEXT("E"), TEXT("mouthE"));
	CurveNameMap.Add(TEXT("O"), TEXT("mouthO"));

	CurveNameMap.Add(TEXT("BrowDownLeft"), TEXT("browDownLeft"));
	CurveNameMap.Add(TEXT("BrowDownRight"), TEXT("browDownRight"));
	CurveNameMap.Add(TEXT("BrowInnerUp"), TEXT("browInnerUp"));
	CurveNameMap.Add(TEXT("BrowOuterUpLeft"), TEXT("browOuterUpLeft"));
	CurveNameMap.Add(TEXT("BrowOuterUpRight"), TEXT("browOuterUpRight"));
	CurveNameMap.Add(TEXT("MouthSmileLeft"), TEXT("mouthSmileLeft"));
	CurveNameMap.Add(TEXT("MouthSmileRight"), TEXT("mouthSmileRight"));
	CurveNameMap.Add(TEXT("MouthFrownLeft"), TEXT("mouthFrownLeft"));
	CurveNameMap.Add(TEXT("MouthFrownRight"), TEXT("mouthFrownRight"));

	MarkPackageDirty();
}

// ---------------- editor hooks ----------------
#if WITH_EDITOR
void UVMCLiveLinkRemapAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	if (!PropertyChangedEvent.Property) return;

	const FName PropName = PropertyChangedEvent.Property->GetFName();
	if (PropName == GET_MEMBER_NAME_CHECKED(UVMCLiveLinkRemapAsset, RemapTable))
	{
		ReloadFromTable(); // Modify + MarkPackageDirty inside
	}
}
#endif
