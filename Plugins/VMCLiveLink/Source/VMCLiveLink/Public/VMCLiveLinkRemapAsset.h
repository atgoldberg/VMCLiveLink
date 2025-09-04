#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "LiveLinkRemapAsset.h"
#include "VMCLiveLinkRemapAsset.generated.h"

UENUM(BlueprintType)
enum class EVMCRemapType : uint8
{
	Bone  UMETA(DisplayName = "Bone"),
	Curve UMETA(DisplayName = "Curve")
};

/** CSV/DataTable schema: Type,Bone|Curve ; Source ; Target */
USTRUCT(BlueprintType)
struct FVMCRemapRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMC Remap")
	EVMCRemapType Type = EVMCRemapType::Bone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMC Remap")
	FName Source;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMC Remap")
	FName Target;
};

/**
 * Data-driven VMC → UE remapper.
 * - Set a DataTable (with FVMCRemapRow rows). We auto-load & keep Bone/Curve maps.
 * - Works in editor (on property change) and at runtime (lazy-load).
 *
 * NOTE: Live Link nodes take a CLASS, not an instance. Use a Blueprint subclass of this,
 * assign the DataTable in its Class Defaults, and you're done.
 */
UCLASS(BlueprintType, Blueprintable, EditInlineNew)
class VMCLIVELINK_API UVMCLiveLinkRemapAsset : public ULiveLinkRemapAsset
{
	GENERATED_BODY()
public:
	/** Optional: CSV/JSON DataTable with rows of FVMCRemapRow (Type/Source/Target). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VMC Remap|Data")
	TObjectPtr<UDataTable> RemapTable = nullptr;

	/** Effective maps used at runtime (populated from DataTable or manual edits). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMC Remap|Names")
	TMap<FName, FName> BoneNameMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VMC Remap|Names")
	TMap<FName, FName> CurveNameMap;

public:
	// LiveLink remap hooks
	virtual FName GetRemappedBoneName_Implementation(FName BoneName) const override;
	virtual FName GetRemappedCurveName_Implementation(FName CurveName) const override;

	// Optional quick presets (still available; no reliance on CallInEditor)
	UFUNCTION(BlueprintCallable, Category = "VMC Remap|Presets")
	void SeedFingerMap_UE();

	UFUNCTION(BlueprintCallable, Category = "VMC Remap|Presets")
	void SeedCurveMap_Common();

	/** Explicit reload if you want it (works in PIE and editor) */
	UFUNCTION(BlueprintCallable, Category = "VMC Remap|Data")
	void ReloadFromTable();

protected:
	/** Ensure maps are loaded (editor or runtime) */
	void EnsureLoaded() const;

	/** Parse the DataTable into BoneNameMap/CurveNameMap */
	void LoadFromTable();

	/** Guard so we only parse once at runtime unless the table changes */
	UPROPERTY(Transient)
	mutable bool bLoadedFromTable = false;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
