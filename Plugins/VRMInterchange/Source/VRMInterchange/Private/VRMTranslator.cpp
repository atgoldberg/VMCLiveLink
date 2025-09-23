#include "VRMTranslator.h"
#include "InterchangeSourceData.h"
#include "Nodes/InterchangeBaseNodeContainer.h"
#include "InterchangeSceneNode.h"

// Use Node APIs available in UE 5.6
#include "InterchangeMeshNode.h"
#include "InterchangeSkeletalMeshFactoryNode.h"
#include "InterchangeSkeletalMeshLodDataNode.h"
#include "InterchangeSkeletonFactoryNode.h"
#include "InterchangeMaterialInstanceNode.h"
#include "InterchangeMaterialFactoryNode.h"
#include "InterchangeShaderGraphNode.h"
#include "InterchangeTexture2DNode.h"
#include "InterchangeTexture2DFactoryNode.h"
#include "InterchangeMaterialDefinitions.h"

// Include payload types here (keep header light)
#include "Mesh/InterchangeMeshPayload.h"
#include "Texture/InterchangeTexturePayloadData.h"

#include "ImageUtils.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/Base64.h"

#include "StaticMeshAttributes.h"
#include "SkeletalMeshAttributes.h"
#include "BoneWeights.h"

#define CGLTF_IMPLEMENTATION
#include "cgltf.h"

// ---- Helper forward declarations (must appear before usage)
static const cgltf_attribute* FindAttribute(const cgltf_primitive* Prim, cgltf_attribute_type Type);
static const cgltf_attribute* FindTexcoord(const cgltf_primitive* Prim, int Set);
static const cgltf_attribute* FindJoints(const cgltf_primitive* Prim, int Set);
static const cgltf_attribute* FindWeights(const cgltf_primitive* Prim, int Set);
static const cgltf_accessor* FindTargetAccessor(const cgltf_morph_target& Tgt, cgltf_attribute_type Type);

static void ReadAccessorVec3f(const cgltf_accessor& A, TArray<FVector3f>& Out);
static void ReadAccessorVec2f(const cgltf_accessor& A, TArray<FVector2f>& Out);
static void ReadIndicesUInt32(const cgltf_accessor& A, TArray<uint32>& Out);
static FTransform NodeTRS(const cgltf_node* N);
static void Normalize4(float W[4]);

// New validation helper (centralized checks)
static bool ValidateCgltfData(const cgltf_data* Data, FString& OutError);

// New forward for extracted image loader
static bool LoadImagesFromCgltf(const cgltf_data* Data, const FString& Filename, FVRMParsedModel& Out);

// New forward for extracted primitive-merge phase
static bool MergePrimitivesFromMeshes(const cgltf_data* Data, const cgltf_skin* Skin, const TMap<int32, int32>& NodeToBone, FVRMParsedModel& Out);

// New forward for extracted morph-target merge phase
static void ParseMorphTargets(const cgltf_data* Data, FVRMParsedModel& Out);

// Simple RAII wrapper to ensure cgltf_free is always called
struct FCgltfScoped
{
    cgltf_data* Data = nullptr;
    explicit FCgltfScoped(cgltf_data* In) : Data(In) {}
    ~FCgltfScoped() { if (Data) cgltf_free(Data); }

    FCgltfScoped(const FCgltfScoped&) = delete;
    FCgltfScoped& operator=(const FCgltfScoped&) = delete;

    FCgltfScoped(FCgltfScoped&& Other) noexcept : Data(Other.Data) { Other.Data = nullptr; }
    FCgltfScoped& operator=(FCgltfScoped&& Other) noexcept
    {
        if (Data) cgltf_free(Data);
        Data = Other.Data;
        Other.Data = nullptr;
        return *this;
    }
};

// glTF->UE axis conversion helpers
static FORCEINLINE FVector3f GltfToUE_Vector(const FVector3f& V)
{
    // glTF: +X Right, +Y Up, +Z Forward
    // UE:   +X Forward, +Y Right, +Z Up
    // Map (Xg,Yg,Zg) -> (Zg, Xg, Yg)
    return FVector3f(V.Z, V.X, V.Y);
}
static FORCEINLINE FVector  GltfToUE_Vector(const FVector& V)
{
    return FVector(V.Z, V.X, V.Y);
}
static FQuat GltfToUE_Quat(const FQuat& Q)
{
    // Rue = C * Rg * C^-1, where C maps glTF basis to UE basis.
    const FMatrix C( FPlane(0,0,1,0),  // col0 -> X components
                     FPlane(1,0,0,0),  // col1 -> Y components
                     FPlane(0,1,0,0),  // col2 -> Z components
                     FPlane(0,0,0,1) );
    const FMatrix CInv = C.GetTransposed();
    const FMatrix Rg = FQuatRotationMatrix(Q);
    const FMatrix Rue = C * Rg * CInv;
    return Rue.ToQuat().GetNormalized();
}

// --- Reference pose fix helpers ---
// Apply a minimal correction so that:
// 1) Left/Right is unmirrored (mirror across Y axis),
// 2) Forward faces +Y (apply +90deg rotation around Z)
static FORCEINLINE FVector3f RefFix_Vector(const FVector3f& V)
{
    // Mirror across Y (flip sign)
    const FVector3f M(V.X, -V.Y, V.Z);
    // Rotate +90 degrees around Z: (x,y,z) -> (-y,x,z)
    return FVector3f(-M.Y, M.X, M.Z);
}
static FORCEINLINE FVector RefFix_Vector(const FVector& V)
{
    const FVector M(V.X, -V.Y, V.Z);
    return FVector(-M.Y, M.X, M.Z);
}

template<typename TWeight>
static void BindAllToRoot(TArray<TWeight>& Weights);

template<typename TWeight>
static void ReadJointsWeights(
    const cgltf_accessor& AJ, const cgltf_accessor& AW,
    const TMap<int32, int32>& NodeToBone,
    TArray<TWeight>& Out);

// Minimal data: URI -> bytes (PNG/JPEG)
static bool DecodeDataUri(const FString& Uri, TArray64<uint8>& OutBytes);

// ===== UInterchangeTranslatorBase =====

TArray<FString> UVRMTranslator::GetSupportedFormats() const
{
    return { TEXT("vrm;VRM Avatar") };
}

EInterchangeTranslatorAssetType UVRMTranslator::GetSupportedAssetTypes() const
{
    // We import scenes that include meshes, skeletons and textures
    return EInterchangeTranslatorAssetType::Meshes | EInterchangeTranslatorAssetType::Textures | EInterchangeTranslatorAssetType::Animations;
}

bool UVRMTranslator::CanImportSourceData(const UInterchangeSourceData* InSourceData) const
{
    const FString Ext = FPaths::GetExtension(InSourceData->GetFilename()).ToLower();
    return (Ext == TEXT("vrm"));
}

bool UVRMTranslator::Translate(UInterchangeBaseNodeContainer& NodeContainer) const
{
    if (!LoadVRM(Parsed))
    {
        UE_LOG(LogTemp, Error, TEXT("[VRMInterchange] Failed to read VRM."));
        return false;
    }

    // Folder subpaths
    const FString PackageSubPath = FPaths::GetBaseFilename(GetSourceData()->GetFilename());
    const FString TexturesSubPath = PackageSubPath / TEXT("Textures");
    const FString MaterialsSubPath = PackageSubPath / TEXT("Materials");

    // Scene root
    const FString SceneNodeUid = MakeNodeUid(TEXT("Scene"));
    UInterchangeSceneNode* SceneNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
    NodeContainer.SetupNode(SceneNode, SceneNodeUid, TEXT("VRMScene"), EInterchangeNodeContainerType::TranslatedScene);
    SceneNode->SetCustomLocalTransform(&NodeContainer, FTransform::Identity);

    // Create skeleton root joint in the scene
    const FString RootJointUid = MakeNodeUid(TEXT("Joint_Root"));
    UInterchangeSceneNode* RootJointNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
    NodeContainer.SetupNode(RootJointNode, RootJointUid, TEXT("VRM_Root"), EInterchangeNodeContainerType::TranslatedScene, SceneNodeUid);
    RootJointNode->SetCustomLocalTransform(&NodeContainer, FTransform::Identity);
    RootJointNode->SetCustomBindPoseLocalTransform(&NodeContainer, FTransform::Identity);
    RootJointNode->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());

    // Build joint hierarchy from parsed bones (if any)
    TArray<FString> BoneUids;
    BoneUids.SetNum(Parsed.Bones.Num());
    for (int32 bi = 0; bi < Parsed.Bones.Num(); ++bi)
    {
        const FVRMParsedBone& B = Parsed.Bones[bi];
        const FString BoneUid = MakeNodeUid(*FString::Printf(TEXT("Joint_%d"), bi));
        BoneUids[bi] = BoneUid;
        UInterchangeSceneNode* BoneNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
        const FString ParentUid = (B.Parent == INDEX_NONE) ? RootJointUid : BoneUids[B.Parent];
        NodeContainer.SetupNode(BoneNode, BoneUid, B.Name.IsEmpty() ? *FString::Printf(TEXT("Bone_%d"), bi) : *B.Name, EInterchangeNodeContainerType::TranslatedScene, ParentUid);
        BoneNode->SetCustomLocalTransform(&NodeContainer, B.LocalBind);
        BoneNode->SetCustomBindPoseLocalTransform(&NodeContainer, B.LocalBind);
        BoneNode->AddSpecializedType(UE::Interchange::FSceneNodeStaticData::GetJointSpecializeTypeString());
    }

    // Textures: add a node for each image discovered in the file and pre-create matching factory nodes for subpath control
    TexturePayloadKeys.Reset();
    TArray<FString> TextureNodeUids;
    TextureNodeUids.SetNum(Parsed.Images.Num());
    for (int32 ti = 0; ti < Parsed.Images.Num(); ++ti)
    {
        const FString TextureKey = FString::Printf(TEXT("Tex_%d"), ti);
        TexturePayloadKeys.Add(TextureKey);
        UInterchangeTexture2DNode* TexNode = NewObject<UInterchangeTexture2DNode>(&NodeContainer);
        const FString TexUid = MakeNodeUid(*TextureKey);
        TextureNodeUids[ti] = TexUid;
        NodeContainer.SetupNode(TexNode, TexUid, *FString::Printf(TEXT("VRM_Tex_%d"), ti), EInterchangeNodeContainerType::TranslatedAsset);
        TexNode->SetPayLoadKey(TextureKey);

#if WITH_EDITORONLY_DATA
        // Pre-create the factory node expected by the pipeline and set Textures subfolder
        const FString TexFactoryUid = UInterchangeTexture2DFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TexUid);
        UInterchangeTexture2DFactoryNode* TexFactory = Cast<UInterchangeTexture2DFactoryNode>(NodeContainer.GetFactoryNode(TexFactoryUid));
        if (!TexFactory)
        {
            TexFactory = NewObject<UInterchangeTexture2DFactoryNode>(&NodeContainer);
            // Initialize as factory data (sets up node in NodeContainer)
            TexFactory->InitializeTextureNode(TexFactoryUid, TexNode->GetDisplayLabel(), TexNode->GetDisplayLabel(), &NodeContainer);
        }
        // Ensure the factory points to the translated texture node so the pipeline can fetch the payload
        TexFactory->SetCustomTranslatedTextureNodeUid(TexUid);
        TexFactory->AddTargetNodeUid(TexUid);
        TexNode->AddTargetNodeUid(TexFactory->GetUniqueID());
        TexFactory->SetCustomSubPath(TexturesSubPath);
#endif
    }

    // Create material shader graph nodes per parsed material
    TArray<FString> MaterialNodeUids;
    MaterialNodeUids.SetNum(Parsed.Materials.Num());
    for (int32 mi = 0; mi < Parsed.Materials.Num(); ++mi)
    {
        const auto& M = Parsed.Materials[mi];
        const FString MatNodeUid = MakeNodeUid(*FString::Printf(TEXT("Mat_%d"), mi));
        MaterialNodeUids[mi] = MatNodeUid;

        // Material (shader graph) node
        UInterchangeShaderGraphNode* MatGraph = NewObject<UInterchangeShaderGraphNode>(&NodeContainer);
        NodeContainer.SetupNode(MatGraph, MatNodeUid, M.Name.IsEmpty() ? *FString::Printf(TEXT("VRM_Mat_%d"), mi) : *M.Name, EInterchangeNodeContainerType::TranslatedAsset);

        // Basic shading settings
        if (M.bDoubleSided) { MatGraph->SetCustomTwoSided(true); }
        // Force Masked blend for VRM; use AlphaCutoff if provided otherwise default to 0.5
        MatGraph->SetCustomBlendMode(EBlendMode::BLEND_Masked);
        if (M.AlphaMode == 1) { MatGraph->SetCustomOpacityMaskClipValue(M.AlphaCutoff); }
        else { MatGraph->SetCustomOpacityMaskClipValue(0.5f); }

        using namespace UE::Interchange::Materials::Standard::Nodes;
        using namespace UE::Interchange::Materials;

        auto AddTextureSample = [&](const FString& NodeName, int32 ImageIndex)->UInterchangeShaderNode*
        {
            if (!Parsed.Images.IsValidIndex(ImageIndex)) return (UInterchangeShaderNode*)nullptr;
            UInterchangeShaderNode* TS = NewObject<UInterchangeShaderNode>(&NodeContainer);
            const FString TexSampleUid = MatNodeUid + TEXT("_") + NodeName;
            NodeContainer.SetupNode(TS, TexSampleUid, NodeName, EInterchangeNodeContainerType::TranslatedAsset, MatNodeUid);
            TS->SetCustomShaderType(TextureSample::Name.ToString());
            TS->AddStringInput(TextureSample::Inputs::Texture.ToString(), TextureNodeUids[ImageIndex], /*bIsAParameter*/ true);
            return TS;
        };

        // BaseColor
        if (M.BaseColorTexture != INDEX_NONE)
        {
            if (UInterchangeShaderNode* TS = AddTextureSample(TEXT("BaseColorTex"), M.BaseColorTexture))
            {
                UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(MatGraph, PBRMR::Parameters::BaseColor.ToString(), TS->GetUniqueID());
                // Also route alpha to OpacityMask to support cutouts from base color alpha
                UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MatGraph, PBRMR::Parameters::OpacityMask.ToString(), TS->GetUniqueID(), TextureSample::Outputs::A.ToString());
            }
        }
        // Normal
        if (M.NormalTexture != INDEX_NONE)
        {
            if (UInterchangeShaderNode* TS = AddTextureSample(TEXT("NormalTex"), M.NormalTexture))
            {
                UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(MatGraph, Common::Parameters::Normal.ToString(), TS->GetUniqueID());
            }
        }
        // MetallicRoughness (metallic in B, roughness in G)
        if (M.MetallicRoughnessTexture != INDEX_NONE)
        {
            if (UInterchangeShaderNode* TS = AddTextureSample(TEXT("MR_Tex"), M.MetallicRoughnessTexture))
            {
                UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MatGraph, PBRMR::Parameters::Roughness.ToString(), TS->GetUniqueID(), TextureSample::Outputs::G.ToString());
                UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MatGraph, PBRMR::Parameters::Metallic.ToString(), TS->GetUniqueID(), TextureSample::Outputs::B.ToString());
            }
        }
        // Occlusion (R)
        if (M.OcclusionTexture != INDEX_NONE)
        {
            if (UInterchangeShaderNode* TS = AddTextureSample(TEXT("AO_Tex"), M.OcclusionTexture))
            {
                UInterchangeShaderPortsAPI::ConnectOuputToInputByName(MatGraph, PBRMR::Parameters::Occlusion.ToString(), TS->GetUniqueID(), TextureSample::Outputs::R.ToString());
            }
        }
        // Emissive
        if (M.EmissiveTexture != INDEX_NONE)
        {
            if (UInterchangeShaderNode* TS = AddTextureSample(TEXT("Emissive_Tex"), M.EmissiveTexture))
            {
                UInterchangeShaderPortsAPI::ConnectDefaultOuputToInput(MatGraph, PBRMR::Parameters::EmissiveColor.ToString(), TS->GetUniqueID());
            }
        }

        // Toon parameters as overridable scalar parameters (so instances can tweak)
        MatGraph->AddFloatInput(TEXT("BandSteps"), 4.0f, /*bIsAParameter*/ true);
        MatGraph->AddFloatInput(TEXT("RimIntensity"), 0.5f, /*bIsAParameter*/ true);
        MatGraph->AddFloatInput(TEXT("RimWidth"), 0.3f, /*bIsAParameter*/ true);

#if WITH_EDITORONLY_DATA
        // Ensure factory node exists for this material (using expected UID) and set Materials subpath
        const FString MatFactoryUid = UInterchangeMaterialFactoryNode::GetMaterialFactoryNodeUidFromMaterialNodeUid(MatNodeUid);
        if (!NodeContainer.GetFactoryNode(MatFactoryUid))
        {
            UInterchangeMaterialFactoryNode* MatFactory = NewObject<UInterchangeMaterialFactoryNode>(&NodeContainer);
            NodeContainer.SetupNode(MatFactory, MatFactoryUid, MatGraph->GetDisplayLabel(), EInterchangeNodeContainerType::TranslatedAsset);
            MatFactory->SetCustomSubPath(MaterialsSubPath);
        }
#endif
    }

    // Name base color textures after their material (suffix _DIFFUSE)
#if WITH_EDITORONLY_DATA
    auto SetTexName = [&](int32 ImageIndex, const FString& Base, const TCHAR* Suffix)
    {
        if (!Parsed.Images.IsValidIndex(ImageIndex)) return;
        const FString TexUid = TextureNodeUids[ImageIndex];
        const FString TexFactoryUid = UInterchangeTexture2DFactoryNode::GetTextureFactoryNodeUidFromTextureNodeUid(TexUid);
        if (UInterchangeTexture2DFactoryNode* TexFactory = Cast<UInterchangeTexture2DFactoryNode>(NodeContainer.GetFactoryNode(TexFactoryUid)))
        {
            TexFactory->SetDisplayLabel(Base + Suffix);
            TexFactory->SetCustomSubPath(TexturesSubPath);
        }
    };
    for (int32 mi = 0; mi < Parsed.Materials.Num(); ++mi)
    {
        const auto& M = Parsed.Materials[mi];
        const FString MatName = M.Name.IsEmpty() ? FString::Printf(TEXT("VRM_Mat_%d"), mi) : M.Name;
        SetTexName(M.BaseColorTexture, MatName, TEXT("_DIFFUSE"));
        SetTexName(M.NormalTexture, MatName, TEXT("_NORMAL"));
        SetTexName(M.MetallicRoughnessTexture, MatName, TEXT("_ORM"));
        SetTexName(M.OcclusionTexture, MatName, TEXT("_AO"));
        SetTexName(M.EmissiveTexture, MatName, TEXT("_EMISSIVE"));
    }
#endif

    // Mesh asset node (SKELETAL)
    const FString MeshNodeUid = MakeNodeUid(TEXT("Mesh_0"));
    UInterchangeMeshNode* MeshNode = NewObject<UInterchangeMeshNode>(&NodeContainer);
    NodeContainer.SetupNode(MeshNode, MeshNodeUid, TEXT("VRM_Mesh"), EInterchangeNodeContainerType::TranslatedAsset);

    MeshPayloadKey = TEXT("VRM_Mesh_0");
    MeshNode->SetPayLoadKey(MeshPayloadKey, EInterchangeMeshPayLoadType::SKELETAL);
    MeshNode->SetSkinnedMesh(true);
    MeshNode->SetSkeletonDependencyUid(RootJointUid);

    // Assign material slots to MATERIAL NODE UIDs (pipeline will resolve to factory nodes)
    for (int32 mi = 0; mi < Parsed.Materials.Num(); ++mi)
    {
        const FString SlotName = FString::Printf(TEXT("MatSlot_%d"), mi);
        MeshNode->SetSlotMaterialDependencyUid(SlotName, MaterialNodeUids[mi]);
    }

    // Scene node that instantiates the mesh
    const FString SkelActorUid = MakeNodeUid(TEXT("SkelActor_0"));
    UInterchangeSceneNode* SkelActorNode = NewObject<UInterchangeSceneNode>(&NodeContainer);
    NodeContainer.SetupNode(SkelActorNode, SkelActorUid, TEXT("VRM_SkeletalActor"), EInterchangeNodeContainerType::TranslatedScene, SceneNodeUid);
    SkelActorNode->SetCustomAssetInstanceUid(MeshNodeUid);
    SkelActorNode->SetCustomLocalTransform(&NodeContainer, FTransform::Identity);

    // Create Interchange mesh nodes for each parsed morph target so the pipeline can request morph payloads
    for (int32 MorphIndex = 0; MorphIndex < Parsed.Mesh.Morphs.Num(); ++MorphIndex)
    {
        const FVRMParsedMorph& PMorph = Parsed.Mesh.Morphs[MorphIndex];
        // Make a node UID unique to this morph
        const FString MorphNodeUid = MakeNodeUid(*FString::Printf(TEXT("Morph_%d"), MorphIndex));
        // Fallback to generated name if none present
        const FString MorphDisplayName = PMorph.Name.IsEmpty() ? FString::Printf(TEXT("VRM_Morph_%d"), MorphIndex) : PMorph.Name;

        // Skip if node already exists
        if (const UInterchangeMeshNode* Existing = Cast<UInterchangeMeshNode>(NodeContainer.GetNode(MorphNodeUid)))
        {
            // Ensure dependency on mesh
            MeshNode->SetMorphTargetDependencyUid(MorphNodeUid);
            continue;
        }

        UInterchangeMeshNode* MorphNode = NewObject<UInterchangeMeshNode>(&NodeContainer);
        NodeContainer.SetupNode(MorphNode, MorphNodeUid, *MorphDisplayName, EInterchangeNodeContainerType::TranslatedAsset);

        // Assign a unique payload key so the pipeline can request payload data for this morph (payload handling can be implemented later)
        const FString MorphPayloadKey = FString::Printf(TEXT("VRM_Morph_%d"), MorphIndex);
        MorphNode->SetPayLoadKey(MorphPayloadKey, EInterchangeMeshPayLoadType::MORPHTARGET);

        // Mark as morph target and set its name
        MorphNode->SetMorphTarget(true);
        MorphNode->SetMorphTargetName(MorphDisplayName);

        // Add dependency on the main mesh so the pipeline knows this morph belongs to the mesh
        MeshNode->SetMorphTargetDependencyUid(MorphNodeUid);
    }

    return true;
}

// ===== Mesh Payload Interface (UE 5.6) =====

TOptional<UE::Interchange::FMeshPayloadData> UVRMTranslator::GetMeshPayloadData(
    const FInterchangeMeshPayLoadKey& PayLoadKey,
    const UE::Interchange::FAttributeStorage& PayloadAttributes) const
{
    using namespace UE::Interchange;

    // Read optional global transform attribute (used by factories)
    FTransform MeshGlobalTransform = FTransform::Identity;
    PayloadAttributes.GetAttribute(UE::Interchange::FAttributeKey{ MeshPayload::Attributes::MeshGlobalTransform }, MeshGlobalTransform);

    const auto& PMesh = Parsed.Mesh;

    // If requester asked for a morph payload, try to identify which morph and serve it
    if (PayLoadKey.Type == EInterchangeMeshPayLoadType::MORPHTARGET)
    {
        const FString Unique = PayLoadKey.UniqueId;
        int32 MorphIndex = INDEX_NONE;

        // Expected key format: "VRM_Morph_%d" as created in Translate()
        const FString Prefix(TEXT("VRM_Morph_"));
        if (Unique.StartsWith(Prefix))
        {
            const FString Suffix = Unique.RightChop(Prefix.Len());
            if (Suffix.IsNumeric())
            {
                MorphIndex = FCString::Atoi(*Suffix);
            }
        }
        else
        {
            // Fallback: try parse trailing integer
            int32 LastUnderscore = INDEX_NONE;
            if (Unique.FindLastChar(TEXT('_'), LastUnderscore) && LastUnderscore != INDEX_NONE)
            {
                const FString Suffix = Unique.Mid(LastUnderscore + 1);
                if (Suffix.IsNumeric())
                {
                    MorphIndex = FCString::Atoi(*Suffix);
                }
            }
        }

        UE_LOG(LogTemp, Verbose, TEXT("[VRMInterchange] Morph payload requested: Key='%s' ParsedIndex=%d"), *Unique, MorphIndex);

        if (MorphIndex == INDEX_NONE || !PMesh.Morphs.IsValidIndex(MorphIndex))
        {
            UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] Invalid morph payload request '%s' (index %d out of range)"), *Unique, MorphIndex);
            return TOptional<UE::Interchange::FMeshPayloadData>();
        }

        const FVRMParsedMorph& PMorph = PMesh.Morphs[MorphIndex];

        // Build a payload MeshDescription that represents the morphed mesh (base + delta)
        FMeshPayloadData Data;
        FMeshDescription& MD = Data.MeshDescription;
        FStaticMeshAttributes StaticAttrs(MD);
        StaticAttrs.Register();
        FSkeletalMeshAttributes SkelAttrs(MD);
        SkelAttrs.Register();

        TVertexAttributesRef<FVector3f> VertexPositions = StaticAttrs.GetVertexPositions();
        TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = StaticAttrs.GetVertexInstanceNormals();
        TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = StaticAttrs.GetVertexInstanceUVs();
        TPolygonGroupAttributesRef<FName> PolyGroupMatSlotNames = StaticAttrs.GetPolygonGroupMaterialSlotNames();

        VertexInstanceUVs.SetNumChannels(1);

        // Create vertices using morphed positions (base + delta)
        TArray<FVertexID> Vertices;
        Vertices.Reserve(PMesh.Positions.Num());

        const bool bHaveDeltas = PMorph.DeltaPositions.Num() == PMesh.Positions.Num();

        for (int32 vi = 0; vi < PMesh.Positions.Num(); ++vi)
        {
            const FVector3f BaseP = PMesh.Positions[vi];
            const FVector3f FinalP = bHaveDeltas ? (BaseP + PMorph.DeltaPositions[vi]) : BaseP;
            const FVertexID V = MD.CreateVertex();
            VertexPositions[V] = FinalP;
            Vertices.Add(V);
        }

        // Create polygon groups per material index used
        TMap<int32, FPolygonGroupID> MatToPG;
        auto GetPGForMat = [&](int32 MatIndex)->FPolygonGroupID
        {
            if (FPolygonGroupID* Found = MatToPG.Find(MatIndex)) return *Found;
            const FPolygonGroupID PG = MD.CreatePolygonGroup();
            const FName SlotName = FName(*FString::Printf(TEXT("MatSlot_%d"), MatIndex));
            PolyGroupMatSlotNames[PG] = SlotName;
            MatToPG.Add(MatIndex, PG);
            return PG;
        };

        // Build triangles mirroring base mesh topology (same indices)
        const int32 TriCount = PMesh.Indices.Num() / 3;
        for (int32 t = 0; t < TriCount; ++t)
        {
            const int32 i0 = PMesh.Indices[t * 3 + 0];
            const int32 i1 = PMesh.Indices[t * 3 + 1];
            const int32 i2 = PMesh.Indices[t * 3 + 2];

            const FVertexInstanceID VI0 = MD.CreateVertexInstance(Vertices[i0]);
            const FVertexInstanceID VI1 = MD.CreateVertexInstance(Vertices[i1]);
            const FVertexInstanceID VI2 = MD.CreateVertexInstance(Vertices[i2]);

            if (PMesh.Normals.IsValidIndex(i0)) VertexInstanceNormals[VI0] = PMesh.Normals[i0];
            if (PMesh.Normals.IsValidIndex(i1)) VertexInstanceNormals[VI1] = PMesh.Normals[i1];
            if (PMesh.Normals.IsValidIndex(i2)) VertexInstanceNormals[VI2] = PMesh.Normals[i2];

            if (PMesh.UV0.IsValidIndex(i0)) VertexInstanceUVs.Set(VI0, 0, PMesh.UV0[i0]);
            if (PMesh.UV0.IsValidIndex(i1)) VertexInstanceUVs.Set(VI1, 0, PMesh.UV0[i1]);
            if (PMesh.UV0.IsValidIndex(i2)) VertexInstanceUVs.Set(VI2, 0, PMesh.UV0[i2]);

            const int32 MatIndex = PMesh.TriMaterialIndex.IsValidIndex(t) ? PMesh.TriMaterialIndex[t] : 0;
            const FPolygonGroupID PG = GetPGForMat(MatIndex);

            MD.CreateTriangle(PG, { VI0, VI1, VI2 });
        }

        // Joint names: use parsed bones, fallback to root only
        Data.JointNames.Reset();
        if (Parsed.Bones.Num() > 0)
        {
            for (const FVRMParsedBone& B : Parsed.Bones)
            {
                Data.JointNames.Add(B.Name.IsEmpty() ? TEXT("Bone") : B.Name);
            }
        }
        else
        {
            Data.JointNames.Add(TEXT("VRM_Root"));
        }

        // Skin weights: copy base mesh weights (same vertex ordering)
        FSkinWeightsVertexAttributesRef SkinWeights = SkelAttrs.GetVertexSkinWeights();
        const int32 NumVerts = MD.Vertices().Num();
        UE::AnimationCore::FBoneWeightsSettings Settings;
        Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);

        if (PMesh.SkinWeights.Num() == NumVerts && Data.JointNames.Num() > 0)
        {
            for (int32 vi = 0; vi < NumVerts; ++vi)
            {
                const FVRMParsedMesh::FWeight& W = PMesh.SkinWeights[vi];
                TArray<UE::AnimationCore::FBoneWeight> BW;
                BW.Reserve(4);
                for (int32 k = 0; k < 4; ++k)
                {
                    const float w = W.Weight[k];
                    if (w > 0.0f)
                    {
                        const int32 BoneIndex = FMath::Clamp<int32>(W.BoneIndex[k], 0, Data.JointNames.Num() - 1);
                        BW.Emplace(BoneIndex, w);
                    }
                }
                SkinWeights.Set(FVertexID(vi), UE::AnimationCore::FBoneWeights::Create(BW, Settings));
            }
        }
        else
        {
            // Fallback: bind to root
            const UE::AnimationCore::FBoneWeight RootInfluence(0, 1.0f);
            const UE::AnimationCore::FBoneWeights RootBinding = UE::AnimationCore::FBoneWeights::Create({ RootInfluence });
            for (const FVertexID VertexID : MD.Vertices().GetElementIDs())
            {
                SkinWeights.Set(VertexID, RootBinding);
            }
        }

        UE_LOG(LogTemp, Verbose, TEXT("[VRMInterchange] Returning MORPHTARGET payload for index %d"), MorphIndex);
        return Data;
    }

    // Default path: return merged base skeletal mesh payload (existing behavior)
    {
        FMeshPayloadData Data;
        FMeshDescription& MD = Data.MeshDescription;
        FStaticMeshAttributes StaticAttrs(MD);
        StaticAttrs.Register();
        FSkeletalMeshAttributes SkelAttrs(MD);
        SkelAttrs.Register();

        TVertexAttributesRef<FVector3f> VertexPositions = StaticAttrs.GetVertexPositions();
        TVertexInstanceAttributesRef<FVector3f> VertexInstanceNormals = StaticAttrs.GetVertexInstanceNormals();
        TVertexInstanceAttributesRef<FVector2f> VertexInstanceUVs = StaticAttrs.GetVertexInstanceUVs();
        TPolygonGroupAttributesRef<FName> PolyGroupMatSlotNames = StaticAttrs.GetPolygonGroupMaterialSlotNames();

        VertexInstanceUVs.SetNumChannels(1);

        // Create vertices
        TArray<FVertexID> Vertices;
        Vertices.Reserve(PMesh.Positions.Num());
        for (const FVector3f& P : PMesh.Positions)
        {
            const FVertexID V = MD.CreateVertex();
            VertexPositions[V] = P;
            Vertices.Add(V);
        }

        // Create polygon groups per material index used
        TMap<int32, FPolygonGroupID> MatToPG;
        auto GetPGForMat = [&](int32 MatIndex)->FPolygonGroupID
        {
            if (FPolygonGroupID* Found = MatToPG.Find(MatIndex)) return *Found;
            const FPolygonGroupID PG = MD.CreatePolygonGroup();
            const FName SlotName = FName(*FString::Printf(TEXT("MatSlot_%d"), MatIndex));
            PolyGroupMatSlotNames[PG] = SlotName;
            MatToPG.Add(MatIndex, PG);
            return PG;
        };

        // Build triangles, route into PGs by TriMaterialIndex
        const int32 TriCount = PMesh.Indices.Num() / 3;
        for (int32 t = 0; t < TriCount; ++t)
        {
            const int32 i0 = PMesh.Indices[t * 3 + 0];
            const int32 i1 = PMesh.Indices[t * 3 + 1];
            const int32 i2 = PMesh.Indices[t * 3 + 2];

            const FVertexInstanceID VI0 = MD.CreateVertexInstance(Vertices[i0]);
            const FVertexInstanceID VI1 = MD.CreateVertexInstance(Vertices[i1]);
            const FVertexInstanceID VI2 = MD.CreateVertexInstance(Vertices[i2]);

            if (PMesh.Normals.IsValidIndex(i0)) VertexInstanceNormals[VI0] = PMesh.Normals[i0];
            if (PMesh.Normals.IsValidIndex(i1)) VertexInstanceNormals[VI1] = PMesh.Normals[i1];
            if (PMesh.Normals.IsValidIndex(i2)) VertexInstanceNormals[VI2] = PMesh.Normals[i2];

            if (PMesh.UV0.IsValidIndex(i0)) VertexInstanceUVs.Set(VI0, 0, PMesh.UV0[i0]);
            if (PMesh.UV0.IsValidIndex(i1)) VertexInstanceUVs.Set(VI1, 0, PMesh.UV0[i1]);
            if (PMesh.UV0.IsValidIndex(i2)) VertexInstanceUVs.Set(VI2, 0, PMesh.UV0[i2]);

            const int32 MatIndex = PMesh.TriMaterialIndex.IsValidIndex(t) ? PMesh.TriMaterialIndex[t] : 0;
            const FPolygonGroupID PG = GetPGForMat(MatIndex);
            // Reverse winding (glTF->UE handedness)
            // Note: We also apply an additional mirror in RefFix, which flips handedness again.
            // So we do NOT reverse the winding here to keep normals facing outward.
            MD.CreateTriangle(PG, { VI0, VI1, VI2 });
        }

        // Skin weights and joint names
        FSkinWeightsVertexAttributesRef SkinWeights = SkelAttrs.GetVertexSkinWeights();

        // Joint names: use parsed bones, fallback to root only
        Data.JointNames.Reset();
        if (Parsed.Bones.Num() > 0)
        {
            for (const FVRMParsedBone& B : Parsed.Bones)
            {
                Data.JointNames.Add(B.Name.IsEmpty() ? TEXT("Bone") : B.Name);
            }
        }
        else
        {
            Data.JointNames.Add(TEXT("VRM_Root"));
        }

        UE::AnimationCore::FBoneWeightsSettings Settings;
        Settings.SetNormalizeType(UE::AnimationCore::EBoneWeightNormalizeType::Always);

        const int32 NumVerts = MD.Vertices().Num();
        if (PMesh.SkinWeights.Num() == NumVerts && Data.JointNames.Num() > 0)
        {
            for (int32 vi = 0; vi < NumVerts; ++vi)
            {
                const FVRMParsedMesh::FWeight& W = PMesh.SkinWeights[vi];
                TArray<UE::AnimationCore::FBoneWeight> BW;
                BW.Reserve(4);
                for (int32 k = 0; k < 4; ++k)
                {
                    const float w = W.Weight[k];
                    if (w > 0.0f)
                    {
                        const int32 BoneIndex = FMath::Clamp<int32>(W.BoneIndex[k], 0, Data.JointNames.Num() - 1);
                        BW.Emplace(BoneIndex, w);
                    }
                }
                SkinWeights.Set(FVertexID(vi), UE::AnimationCore::FBoneWeights::Create(BW, Settings));
            }
        }
        else
        {
            // Fallback: bind to root
            const UE::AnimationCore::FBoneWeight RootInfluence(0, 1.0f);
            const UE::AnimationCore::FBoneWeights RootBinding = UE::AnimationCore::FBoneWeights::Create({ RootInfluence });
            for (const FVertexID VertexID : MD.Vertices().GetElementIDs())
            {
                SkinWeights.Set(VertexID, RootBinding);
            }
        }

        return Data;
    }
}

TOptional<UE::Interchange::FMeshPayloadData> UVRMTranslator::GetMeshPayloadData(
    const FInterchangeMeshPayLoadKey& PayLoadKey,
    const FTransform& MeshGlobalTransform) const
{
    using namespace UE::Interchange;
    FAttributeStorage Attributes;
    Attributes.RegisterAttribute(UE::Interchange::FAttributeKey{ MeshPayload::Attributes::MeshGlobalTransform }, MeshGlobalTransform);
    return GetMeshPayloadData(PayLoadKey, Attributes);
}

// ===== Texture Payload Interface (UE 5.6) =====
TOptional<UE::Interchange::FImportImage> UVRMTranslator::GetTexturePayloadData(const FString& PayloadKey, TOptional<FString>& /*AlternateTexturePath*/) const
{
    using namespace UE::Interchange;

    // Expect keys like "Tex_0"
    if (!PayloadKey.StartsWith(TEXT("Tex_")))
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] Unexpected texture payload key '%s'"), *PayloadKey);
        return {};
    }

    int32 TextureIndex = INDEX_NONE;
    {
        const FString IndexStr = PayloadKey.Mid(4);
        if (!IndexStr.IsNumeric())
        {
            UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] Invalid texture payload key '%s'"), *PayloadKey);
            return {};
        }
        TextureIndex = FCString::Atoi(*IndexStr);
    }

    if (!Parsed.Images.IsValidIndex(TextureIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] Texture index %d out of range for payload '%s'"), TextureIndex, *PayloadKey);
        return {};
    }

    const TArray64<uint8>& CompressedBytes64 = Parsed.Images[TextureIndex].PNGOrJPEGBytes;
    if (CompressedBytes64.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] No image bytes for texture index %d"), TextureIndex);
        return {};
    }

    const uint8* CompressedPtr = CompressedBytes64.GetData();
    const int64  CompressedSize = CompressedBytes64.Num();

    IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
    EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(CompressedPtr, CompressedSize);

    if (ImageFormat == EImageFormat::Invalid)
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] Unknown image format for texture index %d"), TextureIndex);
        return {};
    }

    TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
    if (!Wrapper.IsValid() || !Wrapper->SetCompressed(CompressedPtr, CompressedSize))
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] Failed to decode image for texture index %d"), TextureIndex);
        return {};
    }

    const int32 SizeX = Wrapper->GetWidth();
    const int32 SizeY = Wrapper->GetHeight();

    TArray<uint8> RGBA8;
    if (!Wrapper->GetRaw(ERGBFormat::RGBA, 8, RGBA8))
    {
        UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] GetRaw RGBA8 failed for texture index %d"), TextureIndex);
        return {};
    }

    // Convert RGBA8 -> BGRA8 in-place
    for (int32 i = 0; i + 3 < RGBA8.Num(); i += 4)
    {
        Swap(RGBA8[i + 0], RGBA8[i + 2]); // R <-> B
    }

    FImportImage ImportImage;
    ImportImage.Init2DWithParams(SizeX, SizeY, /*NumMips*/ 1, ETextureSourceFormat::TSF_BGRA8, /*bSRGB*/ true);

    TArrayView64<uint8> Dest = ImportImage.GetArrayViewOfRawData();
    check(Dest.Num() == RGBA8.Num());
    FMemory::Memcpy(Dest.GetData(), RGBA8.GetData(), RGBA8.Num());

    return ImportImage;
}

// ===== Helpers =====

FString UVRMTranslator::MakeNodeUid(const TCHAR* Suffix) const
{
    const FString Base = FPaths::GetBaseFilename(GetSourceData()->GetFilename());
    return FString::Printf(TEXT("VRM_%s_%s"), *Base, Suffix);
}

// Parse + load + validate helper (returns RAII wrapper; OutError receives human-friendly message)
static FCgltfScoped ParseCgltfFile(const FString& Filename, FString& OutError)
{
    OutError.Empty();

    FTCHARToUTF8 PathUtf8(*Filename);
    cgltf_options Options = {};
    cgltf_data* Data = nullptr;

    cgltf_result Res = cgltf_parse_file(&Options, PathUtf8.Get(), &Data);
    if (Res != cgltf_result_success || !Data)
    {
        OutError = FString::Printf(TEXT("cgltf_parse_file failed: %s"), *Filename);
        return FCgltfScoped(nullptr);
    }

    // Wrap immediately so any early returns free Data
    FCgltfScoped ScopedData(Data);

    Res = cgltf_load_buffers(&Options, Data, PathUtf8.Get());
    if (Res != cgltf_result_success)
    {
        OutError = FString::Printf(TEXT("cgltf_load_buffers failed: %s"), *Filename);
        return FCgltfScoped(nullptr);
    }

    // Validate (non-fatal for many glTFs, but keep call to detect issues early)
    cgltf_validate(Data);

    return ScopedData;
}

static bool MergePrimitivesFromMeshes(const cgltf_data* Data, const cgltf_skin* Skin, const TMap<int32, int32>& NodeToBone, FVRMParsedModel& Out)
{
    if (!Data) return false;

    int32 VertexBase = 0;
    for (size_t mi = 0; mi < Data->meshes_count; ++mi)
    {
        const cgltf_mesh* Mesh = &Data->meshes[mi];
        for (size_t pi = 0; pi < Mesh->primitives_count; ++pi)
        {
            const cgltf_primitive* Prim = &Mesh->primitives[pi];

            // POSITION
            TArray<FVector3f> PosLocal; PosLocal.Reset();
            if (const cgltf_attribute* A = FindAttribute(Prim, cgltf_attribute_type_position))
            {
                if (!A->data) { continue; }
                ReadAccessorVec3f(*A->data, PosLocal);
            }
            else { continue; }

            // NORMAL
            TArray<FVector3f> NrmLocal; NrmLocal.Reset();
            if (const cgltf_attribute* A = FindAttribute(Prim, cgltf_attribute_type_normal))
            {
                if (A->data) { ReadAccessorVec3f(*A->data, NrmLocal); }
            }

            // TEXCOORD_0
            TArray<FVector2f> UVLocal; UVLocal.Reset();
            if (const cgltf_attribute* A = FindTexcoord(Prim, 0))
            {
                if (A->data) { ReadAccessorVec2f(*A->data, UVLocal); }
            }
            if (UVLocal.Num() == 0)
            {
                UVLocal.SetNumZeroed(PosLocal.Num());
            }

            const int32 VertCount = PosLocal.Num();

            // Indices for this primitive
            TArray<uint32> IndLocal; IndLocal.Reset();
            if (Prim->indices) { ReadIndicesUInt32(*Prim->indices, IndLocal); }
            else
            {
                IndLocal.SetNum(VertCount);
                for (int32 i = 0; i < VertCount; ++i) { IndLocal[i] = i; }
            }

            // Append converted vertices
            Out.Mesh.Positions.Reserve(Out.Mesh.Positions.Num() + VertCount);
            Out.Mesh.Normals.Reserve(Out.Mesh.Normals.Num() + VertCount);
            Out.Mesh.UV0.Reserve(Out.Mesh.UV0.Num() + VertCount);
            Out.Mesh.SkinWeights.Reserve(Out.Mesh.SkinWeights.Num() + VertCount);

            for (int32 v = 0; v < VertCount; ++v)
            {
                FVector3f Pue = RefFix_Vector(GltfToUE_Vector(PosLocal[v])) * Out.GlobalScale;
                Out.Mesh.Positions.Add(Pue);

                if (NrmLocal.IsValidIndex(v))
                {
                    FVector3f Nue = RefFix_Vector(GltfToUE_Vector(NrmLocal[v]));
                    Out.Mesh.Normals.Add(Nue);
                }
                else
                {
                    Out.Mesh.Normals.Add(FVector3f(0, 0, 1));
                }

                Out.Mesh.UV0.Add(UVLocal[v]);
            }

            // JOINTS/WEIGHTS if available for this primitive
            TArray<FVRMParsedMesh::FWeight> WeightsLocal; WeightsLocal.SetNumZeroed(VertCount);
            bool bHaveJw = false;
            if (Skin)
            {
                const cgltf_attribute* AJ = FindJoints(Prim, 0);
                const cgltf_attribute* AW = FindWeights(Prim, 0);
                if (AJ && AW && AJ->data && AW->data)
                {
                    // Temporary array then append
                    TArray<FVRMParsedMesh::FWeight> Tmp; Tmp.SetNumZeroed(VertCount);
                    ReadJointsWeights(*AJ->data, *AW->data, NodeToBone, Tmp);
                    WeightsLocal = MoveTemp(Tmp);
                    bHaveJw = true;
                }
            }
            for (int32 v = 0; v < VertCount; ++v)
            {
                if (bHaveJw)
                {
                    Out.Mesh.SkinWeights.Add(WeightsLocal[v]);
                }
                else
                {
                    FVRMParsedMesh::FWeight W; W.BoneIndex[0] = 0; W.Weight[0] = 1.f; Out.Mesh.SkinWeights.Add(W);
                }
            }

            // Append indices with offset and record material index for each triangle
            const int32 IndexBase = VertexBase;
            Out.Mesh.Indices.Reserve(Out.Mesh.Indices.Num() + IndLocal.Num());
            int32 MaterialIndex = INDEX_NONE;
            if (Prim->material)
            {
                MaterialIndex = int32(Prim->material - Data->materials);
            }
            for (int32 i = 0; i < IndLocal.Num(); ++i)
            {
                Out.Mesh.Indices.Add(IndexBase + int32(IndLocal[i]));
            }
            const int32 LocalTriCount = IndLocal.Num() / 3;
            for (int32 t = 0; t < LocalTriCount; ++t)
            {
                Out.Mesh.TriMaterialIndex.Add(FMath::Max(0, MaterialIndex));
            }

            VertexBase += VertCount;
        }
    }

    return true;
}

// ---- cgltf-based file load (fill this out) ----
bool UVRMTranslator::LoadVRM(FVRMParsedModel& Out) const
{
    const FString Filename = GetSourceData()->GetFilename();

    Out.GlobalScale = 100.0f;

    // Use helper to parse/load/validate the file and obtain RAII-managed cgltf_data
    FString ParseError;
    FCgltfScoped ScopedData = ParseCgltfFile(Filename, ParseError);
    if (!ScopedData.Data)
    {
        UE_LOG(LogTemp, Error, TEXT("[VRMInterchange] %s"), *ParseError);
        return false;
    }
    cgltf_data* Data = ScopedData.Data;

    // Centralized validation checks
    if (!ValidateCgltfData(Data, ParseError))
    {
        UE_LOG(LogTemp, Error, TEXT("[VRMInterchange] %s"), *ParseError);
        return false;
    }

    // Assume single skin is used by all mesh nodes in VRM
    const cgltf_skin* Skin = (Data->skins_count > 0) ? &Data->skins[0] : nullptr;

    // Reset outputs
    Out.Materials.Reset();
    Out.Images.Reset();
    Out.Mesh.Positions.Reset();
    Out.Mesh.Normals.Reset();
    Out.Mesh.UV0.Reset();
    Out.Mesh.Indices.Reset();
    Out.Mesh.SkinWeights.Reset();
    Out.Mesh.TriMaterialIndex.Reset();

    // Pre-build NodeIndex->BoneIndex mapping if skin exists
    TMap<int32, int32> NodeToBone;
    if (Skin)
    {
        for (size_t ji = 0; ji < Skin->joints_count; ++ji)
        {
            const int32 NodeIndex = int32(Skin->joints[ji] - Data->nodes);
            NodeToBone.Add(NodeIndex, int32(ji));
        }

        // Build bones array from skin joints
        Out.Bones.Reset();
        Out.Bones.SetNum(int32(Skin->joints_count));
        for (int32 ji = 0; ji < int32(Skin->joints_count); ++ji)
        {
            const cgltf_node* J = Skin->joints[ji];
            FVRMParsedBone& B = Out.Bones[ji];
            B.Name = J && J->name ? FString(UTF8_TO_TCHAR(J->name)) : FString::Printf(TEXT("Joint_%d"), ji);

            // Find parent joint within the skin's joint list
            B.Parent = INDEX_NONE;
            const cgltf_node* P = J ? J->parent : nullptr;
            while (P)
            {
                bool bFound = false;
                for (int32 k = 0; k < int32(Skin->joints_count); ++k)
                {
                    if (Skin->joints[k] == P)
                    {
                        B.Parent = k;
                        bFound = true;
                        break;
                    }
                }
                if (bFound) { break; }
                P = P->parent;
            }

            // Convert local bind from glTF to UE axes
            FTransform Local = NodeTRS(J);
            FVector T = GltfToUE_Vector(Local.GetTranslation()) * Out.GlobalScale;
            FQuat R = GltfToUE_Quat(Local.GetRotation());
            FVector S = GltfToUE_Vector(Local.GetScale3D());
            B.LocalBind = FTransform(R, T, S);
        }

        // Rebuild bone local binds so that:
        //  - All local rotations are identity
        //  - Global joint positions are corrected to face +Y and be unmirrored L/R
        {
            const int32 NumBones = Out.Bones.Num();
            TArray<FTransform> GlobalXf; GlobalXf.SetNum(NumBones);
            TArray<FVector>    FixedGlobalPos; FixedGlobalPos.SetNum(NumBones);

            // Compute original global transforms from current local binds
            for (int32 i = 0; i < NumBones; ++i)
            {
                const int32 Parent = Out.Bones[i].Parent;
                const FTransform ParentGlobal = (Parent == INDEX_NONE) ? FTransform::Identity : GlobalXf[Parent];
                GlobalXf[i] = Out.Bones[i].LocalBind * ParentGlobal;
                // Apply reference fix to the global position
                FixedGlobalPos[i] = RefFix_Vector(GlobalXf[i].GetTranslation());
            }

            // Recreate local binds as pure translations (identity rotation), preserving corrected positions
            for (int32 i = 0; i < NumBones; ++i)
            {
                const int32 Parent = Out.Bones[i].Parent;
                const FVector ParentPos = (Parent == INDEX_NONE) ? FVector::ZeroVector : FixedGlobalPos[Parent];
                const FVector LocalT = FixedGlobalPos[i] - ParentPos;
                Out.Bones[i].LocalBind = FTransform(FQuat::Identity, LocalT, FVector(1,1,1));
            }
        }
    }
    else
    {
        Out.Bones.Reset();
    }

    // Merge all primitives from all meshes (extracted)
    if (!MergePrimitivesFromMeshes(Data, Skin, NodeToBone, Out))
    {
        UE_LOG(LogTemp, Error, TEXT("[VRMInterchange] Failed to merge mesh primitives."));
        return false;
    }

    // Parse morph targets (extracted helper) — must run after primitives merged
    ParseMorphTargets(Data, Out);

    // Images: use extracted helper
    if (Data->images_count > 0)
    {
        LoadImagesFromCgltf(Data, Filename, Out);
    }

    // Materials: record at least base-color texture indices if available (optional)
    Out.Materials.Reset();
    for (int32 mi = 0; mi < int32(Data->materials_count); ++mi)
    {
        const cgltf_material* Mat = &Data->materials[mi];
        FVRMParsedModel::FMat M;
        M.Name = Mat->name ? FString(UTF8_TO_TCHAR(Mat->name)) : FString::Printf(TEXT("VRM_Mat_%d"), mi);
        M.BaseColorTexture = INDEX_NONE;
        M.NormalTexture = INDEX_NONE;
        M.MetallicRoughnessTexture = INDEX_NONE;
        M.OcclusionTexture = INDEX_NONE;
        M.EmissiveTexture = INDEX_NONE;
        if (Mat->has_pbr_metallic_roughness)
        {
            if (Mat->pbr_metallic_roughness.base_color_texture.texture && Mat->pbr_metallic_roughness.base_color_texture.texture->image)
            {
                M.BaseColorTexture = int32(Mat->pbr_metallic_roughness.base_color_texture.texture->image - Data->images);
            }
            if (Mat->pbr_metallic_roughness.metallic_roughness_texture.texture && Mat->pbr_metallic_roughness.metallic_roughness_texture.texture->image)
            {
                M.MetallicRoughnessTexture = int32(Mat->pbr_metallic_roughness.metallic_roughness_texture.texture->image - Data->images);
            }
        }
        if (Mat->normal_texture.texture && Mat->normal_texture.texture->image)
        {
            M.NormalTexture = int32(Mat->normal_texture.texture->image - Data->images);
        }
        if (Mat->occlusion_texture.texture && Mat->occlusion_texture.texture->image)
        {
            M.OcclusionTexture = int32(Mat->occlusion_texture.texture->image - Data->images);
        }
        if (Mat->emissive_texture.texture && Mat->emissive_texture.texture->image)
        {
            M.EmissiveTexture = int32(Mat->emissive_texture.texture->image - Data->images);
        }
        M.bDoubleSided = Mat->double_sided != 0;
        if (Mat->alpha_mode == cgltf_alpha_mode_mask)
        {
            M.AlphaMode = 1;
            M.AlphaCutoff = (float)Mat->alpha_cutoff;
        }
        else if (Mat->alpha_mode == cgltf_alpha_mode_blend)
        {
            M.AlphaMode = 2;
        }
        Out.Materials.Add(M);
    }

    return true;
}

// New validation helper (centralized checks)
static bool ValidateCgltfData(const cgltf_data* Data, FString& OutError)
{
    OutError.Empty();
    if (!Data)
    {
        OutError = TEXT("No glTF data (null).");
        return false;
    }

    if (Data->meshes_count == 0 || Data->nodes_count == 0)
    {
        OutError = TEXT("No meshes or nodes in file.");
        return false;
    }

    // Ensure every mesh has at least one primitive and each primitive has a POSITION attribute.
    for (size_t mi = 0; mi < Data->meshes_count; ++mi)
    {
        const cgltf_mesh* Mesh = &Data->meshes[mi];
        if (Mesh->primitives_count == 0)
        {
            OutError = FString::Printf(TEXT("Mesh %d contains no primitives."), int(mi));
            return false;
        }

        for (size_t pi = 0; pi < Mesh->primitives_count; ++pi)
        {
            const cgltf_primitive* Prim = &Mesh->primitives[pi];

            bool bHasPosition = false;
            for (size_t ai = 0; ai < Prim->attributes_count; ++ai)
            {
                if (Prim->attributes[ai].type == cgltf_attribute_type_position)
                {
                    bHasPosition = true;
                    break;
                }
            }

            if (!bHasPosition)
            {
                OutError = FString::Printf(TEXT("Primitive %d.%d missing POSITION attribute."), int(mi), int(pi));
                return false;
            }
        }
    }

    return true;
}

// Attribute finders
static const cgltf_attribute* FindAttribute(const cgltf_primitive* Prim, cgltf_attribute_type Type)
{
    for (size_t i = 0; i < Prim->attributes_count; ++i)
        if (Prim->attributes[i].type == Type) return &Prim->attributes[i];
    return nullptr;
}
static const cgltf_attribute* FindTexcoord(const cgltf_primitive* Prim, int Set)
{
    for (size_t i = 0; i < Prim->attributes_count; ++i)
        if (Prim->attributes[i].type == cgltf_attribute_type_texcoord && (int)Prim->attributes[i].index == Set)
            return &Prim->attributes[i];
    return nullptr;
}
static const cgltf_attribute* FindJoints(const cgltf_primitive* Prim, int Set)
{
    for (size_t i = 0; i < Prim->attributes_count; ++i)
        if (Prim->attributes[i].type == cgltf_attribute_type_joints && (int)Prim->attributes[i].index == Set)
            return &Prim->attributes[i];
    return nullptr;
}
static const cgltf_attribute* FindWeights(const cgltf_primitive* Prim, int Set)
{
    for (size_t i = 0; i < Prim->attributes_count; ++i)
        if (Prim->attributes[i].type == cgltf_attribute_type_weights && (int)Prim->attributes[i].index == Set)
            return &Prim->attributes[i];
    return nullptr;
}
static const cgltf_accessor* FindTargetAccessor(const cgltf_morph_target& Tgt, cgltf_attribute_type Type)
{
    for (size_t i = 0; i < Tgt.attributes_count; ++i)
        if (Tgt.attributes[i].type == Type) return Tgt.attributes[i].data;
    return nullptr;
}

// Accessor readers
static void ReadAccessorVec3f(const cgltf_accessor& A, TArray<FVector3f>& Out)
{
    Out.SetNumUninitialized(int32(A.count));
    float v[3] = {0,0,0};
    for (int32 i = 0; i < (int32)A.count; ++i)
    {
        cgltf_accessor_read_float(&A, i, v, 3);
        Out[i] = FVector3f(v[0], v[1], v[2]);
    }
}
static void ReadAccessorVec2f(const cgltf_accessor& A, TArray<FVector2f>& Out)
{
    Out.SetNumUninitialized(int32(A.count));
    float v[2] = {0,0};
    for (int32 i = 0; i < (int32)A.count; ++i)
    {
        cgltf_accessor_read_float(&A, i, v, 2);
        Out[i] = FVector2f(v[0], v[1]);
    }
}
static void ReadIndicesUInt32(const cgltf_accessor& A, TArray<uint32>& Out)
{
    Out.SetNumUninitialized(int32(A.count));
    for (int32 i = 0; i < (int32)A.count; ++i)
    {
        cgltf_uint v[1] = {0};
        cgltf_accessor_read_uint(&A, i, v, 1);
        Out[i] = (uint32)v[0];
    }
}
static FTransform NodeTRS(const cgltf_node* N)
{
    FVector T(0, 0, 0);
    FQuat   R = FQuat::Identity;
    FVector S(1, 1, 1);

    if (N && N->has_translation) { T = FVector(N->translation[0], N->translation[1], N->translation[2]); }
    if (N && N->has_rotation) { R = FQuat(N->rotation[0], N->rotation[1], N->rotation[2], N->rotation[3]); }
    if (N && N->has_scale) { S = FVector(N->scale[0], N->scale[1], N->scale[2]); }

    FTransform Xf; Xf.SetComponents(R, T, S);
    return Xf;
}
static void Normalize4(float W[4])
{
    const float Sum = FMath::Max(1e-8f, W[0] + W[1] + W[2] + W[3]);
    W[0] /= Sum; W[1] /= Sum; W[2] /= Sum; W[3] /= Sum;
}

template<typename TWeight>
static void BindAllToRoot(TArray<TWeight>& Weights)
{
    for (auto& W : Weights)
    {
        W.BoneIndex[0] = 0; W.Weight[0] = 1.f;
        for (int k = 1; k < 4; ++k) { W.BoneIndex[k] = 0; W.Weight[k] = 0.f; }
    }
}

template<typename TWeight>
static void ReadJointsWeights(
    const cgltf_accessor& AJ, const cgltf_accessor& AW,
    const TMap<int32, int32>& NodeToBone,
    TArray<TWeight>& Out)
{
    const int32 Count = int32(AJ.count);
    check(Count == int32(AW.count));
    check(Count == Out.Num());

    for (int32 i = 0; i < Count; ++i)
    {
        uint32 J[4] = { 0,0,0,0 };
        cgltf_accessor_read_uint(&AJ, i, (cgltf_uint*)J, 4);

        float W[4] = { 0,0,0,0 };
        cgltf_accessor_read_float(&AW, i, W, 4);
        Normalize4(W);

        auto& Dst = Out[i];
        for (int k = 0; k < 4; ++k)
        {
            int32 Bone = 0;
            if (const int32* Found = NodeToBone.Find(int32(J[k])))
                Bone = *Found;
            Dst.BoneIndex[k] = uint16(Bone);
            Dst.Weight[k] = W[k];
        }
    }
}

static bool DecodeDataUri(const FString& Uri, TArray64<uint8>& OutBytes)
{
    int32 Comma = INDEX_NONE;
    if (!Uri.FindChar(TEXT(','), Comma)) return false;
    const FString Base64 = Uri.Mid(Comma + 1);

    TArray<uint8> Temp;
    if (!FBase64::Decode(Base64, Temp))
    {
        return false;
    }
    OutBytes.SetNumUninitialized(Temp.Num());
    if (Temp.Num() > 0)
    {
        FMemory::Memcpy(OutBytes.GetData(), Temp.GetData(), Temp.Num());
    }
    return true;
}

// Extracted image loader
static bool LoadImagesFromCgltf(const cgltf_data* Data, const FString& Filename, FVRMParsedModel& Out)
{
    if (!Data) return false;

    if (Data->images_count > 0)
    {
        Out.Images.SetNum(int32(Data->images_count));
        for (int32 ii = 0; ii < int32(Data->images_count); ++ii)
        {
            const cgltf_image* Img = &Data->images[ii];
            FVRMParsedImage P;
            P.Name = Img->name ? FString(UTF8_TO_TCHAR(Img->name)) : FString::Printf(TEXT("Image_%d"), ii);

            if (Img->buffer_view)
            {
                const uint8* Ptr = (const uint8*)Img->buffer_view->buffer->data + Img->buffer_view->offset;
                const size_t Size = Img->buffer_view->size;
                P.PNGOrJPEGBytes.SetNumUninitialized(Size);
                FMemory::Memcpy(P.PNGOrJPEGBytes.GetData(), Ptr, Size);
            }
            else if (Img->uri)
            {
                const FString Uri = UTF8_TO_TCHAR(Img->uri);
                if (Uri.StartsWith(TEXT("data:")))
                {
                    DecodeDataUri(Uri, P.PNGOrJPEGBytes);
                }
                else
                {
                    const FString Dir = FPaths::GetPath(Filename);
                    const FString ImgPath = FPaths::ConvertRelativePathToFull(Dir / Uri);
                    FFileHelper::LoadFileToArray(P.PNGOrJPEGBytes, *ImgPath);
                }
            }
            Out.Images[ii] = MoveTemp(P);
        }
    }
    return true;
}

static void ParseMorphTargets(const cgltf_data* Data, FVRMParsedModel& Out)
{
    if (!Data) return;

    const int32 TotalVertices = Out.Mesh.Positions.Num();
    if (TotalVertices <= 0) return;

    // Map target name -> global morph index
    TMap<FString, int32> NameToIndex;
    // Keep ordered list of names to create Out.Mesh.Morphs in deterministic order
    TArray<FString> OrderedNames;

    // First pass: discover all target names (if available) and build mapping.
    for (size_t mi2 = 0; mi2 < Data->meshes_count; ++mi2)
    {
        const cgltf_mesh* Mesh2 = &Data->meshes[mi2];
        const bool bHaveMeshNames = (Mesh2 && Mesh2->target_names && Mesh2->target_names_count > 0);

        for (size_t pi2 = 0; pi2 < Mesh2->primitives_count; ++pi2)
        {
            const cgltf_primitive* Prim2 = &Mesh2->primitives[pi2];
            for (size_t ti = 0; ti < Prim2->targets_count; ++ti)
            {
                FString TargetName;
                if (bHaveMeshNames && ti < Mesh2->target_names_count && Mesh2->target_names[ti])
                {
                    TargetName = FString(UTF8_TO_TCHAR(Mesh2->target_names[ti])).TrimStartAndEnd();
                }
                // If no name available, use deterministic index-based fallback so unnamed targets still group by index
                if (TargetName.IsEmpty())
                {
                    TargetName = FString::Printf(TEXT("morph_%d"), int32(ti));
                }

                if (!NameToIndex.Contains(TargetName))
                {
                    const int32 NewIdx = OrderedNames.Num();
                    OrderedNames.Add(TargetName);
                    NameToIndex.Add(TargetName, NewIdx);
                }
            }
        }
    }

    // If no targets discovered, nothing to do.
    if (OrderedNames.Num() == 0) return;

    // Allocate global morphs and zero the delta arrays
    Out.Mesh.Morphs.SetNum(OrderedNames.Num());
    for (int32 mi = 0; mi < OrderedNames.Num(); ++mi)
    {
        Out.Mesh.Morphs[mi].Name = OrderedNames[mi];
        Out.Mesh.Morphs[mi].DeltaPositions.SetNumZeroed(TotalVertices);
    }

    // Second pass: read per-primitive deltas and merge into the global morph identified by name (or fallback index-name)
    int32 VertexBase2 = 0;
    for (size_t mi2 = 0; mi2 < Data->meshes_count; ++mi2)
    {
        const cgltf_mesh* Mesh2 = &Data->meshes[mi2];
        const bool bHaveMeshNames = (Mesh2 && Mesh2->target_names && Mesh2->target_names_count > 0);

        for (size_t pi2 = 0; pi2 < Mesh2->primitives_count; ++pi2)
        {
            const cgltf_primitive* Prim2 = &Mesh2->primitives[pi2];

            // POSITION accessor to determine this primitive's vertex count
            int32 PrimVertCount = 0;
            if (const cgltf_attribute* Apos = FindAttribute(Prim2, cgltf_attribute_type_position))
            {
                if (Apos->data)
                {
                    PrimVertCount = (int32)Apos->data->count;
                }
            }

            for (size_t ti = 0; ti < Prim2->targets_count; ++ti)
            {
                // Determine global morph name/key for this primitive target
                FString TargetName;
                if (bHaveMeshNames && ti < Mesh2->target_names_count && Mesh2->target_names[ti])
                {
                    TargetName = FString(UTF8_TO_TCHAR(Mesh2->target_names[ti])).TrimStartAndEnd();
                }
                if (TargetName.IsEmpty())
                {
                    TargetName = FString::Printf(TEXT("morph_%d"), int32(ti));
                }

                const int32* FoundGlobal = NameToIndex.Find(TargetName);
                if (!FoundGlobal)
                {
                    // Shouldn't happen, but guard
                    continue;
                }
                const int32 GlobalMorphIndex = *FoundGlobal;

                const cgltf_morph_target& Tgt = Prim2->targets[ti];
                const cgltf_accessor* PosAcc = FindTargetAccessor(Tgt, cgltf_attribute_type_position);
                if (!PosAcc || !PosAcc->count)
                {
                    continue;
                }

                TArray<FVector3f> DeltaLocal;
                ReadAccessorVec3f(*PosAcc, DeltaLocal);

                if (DeltaLocal.Num() != PrimVertCount)
                {
                    UE_LOG(LogTemp, Warning, TEXT("[VRMInterchange] Morph target vertex count mismatch (primitive %d.%d): %d vs %d. Skipping."), (int)mi2, (int)pi2, DeltaLocal.Num(), PrimVertCount);
                    continue;
                }

                for (int32 v = 0; v < PrimVertCount; ++v)
                {
                    const FVector3f Src = DeltaLocal[v];
                    const FVector3f Conv = RefFix_Vector(GltfToUE_Vector(Src)) * Out.GlobalScale;
                    const int32 GlobalIndex = VertexBase2 + v;
                    if (Out.Mesh.Morphs.IsValidIndex(GlobalMorphIndex) && Out.Mesh.Morphs[GlobalMorphIndex].DeltaPositions.IsValidIndex(GlobalIndex))
                    {
                        Out.Mesh.Morphs[GlobalMorphIndex].DeltaPositions[GlobalIndex] = Conv;
                    }
                }
            }

            VertexBase2 += PrimVertCount;
        }
    }
}
