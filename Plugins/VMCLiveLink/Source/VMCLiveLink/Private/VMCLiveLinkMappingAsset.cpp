// Copyright (c) 2025 ...
#include "VMCLiveLinkMappingAsset.h"

#if WITH_EDITOR
#include "Animation/Skeleton.h"
#endif

#if WITH_EDITOR
static FString NormalizeBoneName(FString S)
{
	S = S.ToLower();
	S.ReplaceInline(TEXT("_"), TEXT(""));
	S.ReplaceInline(TEXT("-"), TEXT(""));
	return S;
}

uint32 UVMCLiveLinkMappingAsset::ComputeSignature(const USkeletalMesh* Mesh)
{
	if (!Mesh) return 0u;

	const FReferenceSkeleton& RefSkel = Mesh->GetRefSkeleton();
	TArray<FString> NormNames;
	NormNames.Reserve(RefSkel.GetNum());
	for (int32 i = 0; i < RefSkel.GetNum(); ++i)
	{
		NormNames.Add(NormalizeBoneName(RefSkel.GetBoneName(i).ToString()));
	}
	NormNames.Sort();

	// Stable hash over the normalized sorted set of bone names
	uint32 H = 1469598103u;
	for (const FString& N : NormNames)
	{
		H = HashCombine(H, GetTypeHash(N));
	}
	// Include count for safety
	H = HashCombine(H, GetTypeHash(RefSkel.GetNum()));
	return H;
}

void UVMCLiveLinkMappingAsset::CaptureSignatureFrom(USkeletalMesh* Mesh)
{
	if (!Mesh) return;
	const uint32 Sig = ComputeSignature(Mesh);
	if (Sig != 0u)
	{
		SkeletonSignatures.AddUnique(Sig);
	}
	// Keep an example mesh reference too; helps user readability and alternate matching
	ExampleReferenceMeshes.AddUnique(Mesh);
	Modify();
}

bool UVMCLiveLinkMappingAsset::MatchesMesh(USkeletalMesh* Mesh) const
{
	if (!Mesh) return false;

	// Direct soft references win
	for (const TSoftObjectPtr<USkeletalMesh>& Soft : ExampleReferenceMeshes)
	{
		if (Soft.ToSoftObjectPath() == Mesh)
		{
			return true;
		}
		if (USkeletalMesh* L = Soft.Get())
		{
			if (L == Mesh) return true;
		}
	}

	// Signature match
	const uint32 Sig = ComputeSignature(Mesh);
	return Sig != 0u && SkeletonSignatures.Contains(Sig);
}
#endif