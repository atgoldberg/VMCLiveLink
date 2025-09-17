// Copyright (c) 2025 ...
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Engine/SkeletalMesh.h"
#include "VMCLiveLinkMappingAsset.generated.h"

UCLASS(BlueprintType)
class VMCLIVELINK_API UVMCLiveLinkMappingAsset : public UDataAsset
{
	GENERATED_BODY()
public:
	// The reusable maps users will maintain in an asset
	UPROPERTY(EditAnywhere, Category="Mapping")
	TMap<FName, FName> BoneNameMap;

	UPROPERTY(EditAnywhere, Category="Mapping")
	TMap<FName, FName> CurveNameMap;

	// Hint meshes that this mapping applies to (optional; used for auto-detect)
	UPROPERTY(EditAnywhere, Category="Detection")
	TArray<TSoftObjectPtr<USkeletalMesh>> ExampleReferenceMeshes;

	// Lightweight signatures of skeletons this mapping applies to (auto-detect)
	UPROPERTY(EditAnywhere, Category="Detection")
	TArray<uint32> SkeletonSignatures;

#if WITH_EDITOR
	// Compute a signature from a mesh and add it to this asset (Call In Editor)
	UFUNCTION(CallInEditor, Category="Detection")
	void CaptureSignatureFrom(USkeletalMesh* Mesh);

	// Check if the asset is a match for a given mesh
	UFUNCTION(BlueprintCallable, Category="Detection")
	bool MatchesMesh(USkeletalMesh* Mesh) const;

	// Utility: compute a normalized signature for a mesh's RefSkeleton
	static uint32 ComputeSignature(const USkeletalMesh* Mesh);
#endif
};