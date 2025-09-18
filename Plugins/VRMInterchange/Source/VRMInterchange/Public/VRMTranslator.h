#pragma once

#include "CoreMinimal.h"
#include "InterchangeTranslatorBase.h"
#include "Mesh/InterchangeMeshPayloadInterface.h"
#include "Texture/InterchangeTexturePayloadInterface.h"

// Forward declares to avoid including heavy headers here
namespace UE::Interchange
{
    struct FMeshPayloadData;
    struct FImportImage;
    class FAttributeStorage;
}
struct FInterchangeMeshPayLoadKey;

// ---- Minimal parsed model data (fill from cgltf) ----
struct FVRMParsedImage
{
    FString Name;
    TArray64<uint8> PNGOrJPEGBytes; // decoded later through ImageWrapper
};

struct FVRMParsedMorph
{
    FString Name;
    TArray<FVector3f> DeltaPositions;
};

struct FVRMParsedMesh
{
    TArray<FVector3f> Positions;
    TArray<FVector3f> Normals;
    TArray<FVector2f> UV0;
    TArray<uint32> Indices;

    // Per-triangle material index (Indices.Num()/3 entries). Refers to FVRMParsedModel::Materials index.
    TArray<int32> TriMaterialIndex;

    struct FWeight
    {
        uint16 BoneIndex[4] = { 0,0,0,0 };
        float  Weight[4] = { 1,0,0,0 };
    };
    TArray<FWeight> SkinWeights;

    TArray<FVRMParsedMorph> Morphs;

    int32 MaterialIndex = 0;
};

struct FVRMParsedBone
{
    FString Name;
    int32 Parent = INDEX_NONE;
    FTransform LocalBind;
};

struct FVRMParsedModel
{
    TArray<FVRMParsedBone> Bones;
    TArray<FVRMParsedImage> Images;

    struct FMat
    {
        FString Name;
        int32 BaseColorTexture = INDEX_NONE;
        int32 NormalTexture = INDEX_NONE;
        int32 MetallicRoughnessTexture = INDEX_NONE; // G=Roughness, B=Metallic
        int32 OcclusionTexture = INDEX_NONE; // R channel
        int32 EmissiveTexture = INDEX_NONE;
        bool bDoubleSided = false;
        int32 AlphaMode = 0; // 0 Opaque, 1 Mask, 2 Blend
        float AlphaCutoff = 0.5f;
    };
    TArray<FMat> Materials;

    // Single merged mesh for now
    FVRMParsedMesh Mesh;

    float GlobalScale = 100.f;
};

#include "VRMTranslator.generated.h"

UCLASS()
class UVRMTranslator : public UInterchangeTranslatorBase
    , public IInterchangeMeshPayloadInterface
    , public IInterchangeTexturePayloadInterface
{
    GENERATED_BODY()

public:
    // UInterchangeTranslatorBase
    virtual EInterchangeTranslatorType GetTranslatorType() const override { return EInterchangeTranslatorType::Scenes; }
    virtual EInterchangeTranslatorAssetType GetSupportedAssetTypes() const override;
    virtual TArray<FString> GetSupportedFormats() const override;
    virtual bool CanImportSourceData(const UInterchangeSourceData* InSourceData) const override;
    virtual bool Translate(UInterchangeBaseNodeContainer& NodeContainer) const override;

    // IInterchangeMeshPayloadInterface (UE 5.6)
    UE_DEPRECATED(5.6, "Deprecated. Use GetMeshPayloadData(const FInterchangeMeshPayLoadKey&, const UE::Interchange::FAttributeStorage&) instead.")
    virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(const FInterchangeMeshPayLoadKey& PayLoadKey, const FTransform& MeshGlobalTransform) const override;

    virtual TOptional<UE::Interchange::FMeshPayloadData> GetMeshPayloadData(const FInterchangeMeshPayLoadKey& PayLoadKey, const UE::Interchange::FAttributeStorage& PayloadAttributes) const override;

    // IInterchangeTexturePayloadInterface (UE 5.6)
    virtual TOptional<UE::Interchange::FImportImage> GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& AlternateTexturePath) const override;

private:
    bool LoadVRM(FVRMParsedModel& Out) const;

    // cache payloads after load
    mutable FVRMParsedModel Parsed;
    mutable TArray<FString> TexturePayloadKeys;
    mutable FString MeshPayloadKey;

    FString MakeNodeUid(const TCHAR* Suffix) const;
};
