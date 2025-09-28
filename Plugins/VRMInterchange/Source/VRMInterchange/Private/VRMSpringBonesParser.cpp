#include "VRMSpringBonesParser.h"
#include "VRMInterchangeLog.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"

namespace
{
    static bool ExtractTopLevelJsonString(const FString& Filename, FString& OutJson)
    {
        OutJson.Empty();

        const FString Ext = FPaths::GetExtension(Filename).ToLower();
        if (Ext == TEXT("gltf"))
        {
            return FFileHelper::LoadFileToString(OutJson, *Filename);
        }

        TArray<uint8> Bytes;
        if (!FFileHelper::LoadFileToArray(Bytes, *Filename) || Bytes.Num() < 20)
        {
            return false;
        }

        auto ReadLE32 = [](const uint8* p)->uint32
        {
            return (uint32)p[0] | ((uint32)p[1] << 8) | ((uint32)p[2] << 16) | ((uint32)p[3] << 24);
        };

        const uint8* Ptr = Bytes.GetData();
        const uint32 Magic = ReadLE32(Ptr + 0);
        const uint32 Version = ReadLE32(Ptr + 4);
        const uint32 Length = ReadLE32(Ptr + 8);
        if (Magic != 0x46546C67 || Version != 2 || Length != (uint32)Bytes.Num())
        {
            return false;
        }

        const uint32 Chunk0Len = ReadLE32(Ptr + 12);
        const uint32 Chunk0Type = ReadLE32(Ptr + 16);
        if (Bytes.Num() < 20 + (int64)Chunk0Len || Chunk0Type != 0x4E4F534A /*JSON*/)
        {
            return false;
        }

        const uint8* JsonStart = Ptr + 20;
        int32 JsonLen = (int32)Chunk0Len;

        while (JsonLen > 0 && (JsonStart[JsonLen - 1] == 0 || JsonStart[JsonLen - 1] == ' ' || JsonStart[JsonLen - 1] == '\n' || JsonStart[JsonLen - 1] == '\r' || JsonStart[JsonLen - 1] == '\t'))
        {
            --JsonLen;
        }
        if (JsonLen <= 0) return false;

        if (JsonLen >= 3 && JsonStart[0] == 0xEF && JsonStart[1] == 0xBB && JsonStart[2] == 0xBF)
        {
            JsonStart += 3;
            JsonLen -= 3;
        }

        FUTF8ToTCHAR Conv((const ANSICHAR*)JsonStart, JsonLen);
        OutJson = FString(Conv.Length(), Conv.Get());
        return !OutJson.IsEmpty();
    }

    static FVector ReadVec3(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, const FVector& Default = FVector::ZeroVector)
    {
        const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
        if (!Obj.IsValid() || !Obj->TryGetArrayField(Field, Arr) || !Arr || Arr->Num() < 3) return Default;
        auto GetF = [](const TSharedPtr<FJsonValue>& V)->double { return V.IsValid() ? V->AsNumber() : 0.0; };
        return FVector((float)GetF((*Arr)[0]), (float)GetF((*Arr)[1]), (float)GetF((*Arr)[2]));
    }

    // Convert a direction from glTF/VRM coordinate space into Unreal coordinate space (Z-up)
    // glTF: (X right, Y up, Z forward) -> Unreal: (X forward, Y right, Z up)
    // Mapping used here: UE = (glTF.Z, glTF.X, glTF.Y)
    static FORCEINLINE FVector GltfToUE_Dir(const FVector& V)
    {
        return FVector(V.Z, V.X, V.Y);
    }

    // Helper: Some exporters wrap shapes as { "sphere": {..} } or { "capsule": {..} }
    // Others use { "shape": { "sphere": {..} } } or { "shape": { "capsule": {..} } }
    static void ParseOneShapeObject(const TSharedPtr<FJsonObject>& ShapeEntry,
                                    TArray<FVRMSpringColliderSphere>& OutSpheres,
                                    TArray<FVRMSpringColliderCapsule>& OutCapsules,
                                    TArray<FVRMSpringColliderPlane>& OutPlanes)
    {
        if (!ShapeEntry.IsValid()) return;

        auto ParseFromContainer = [&OutSpheres, &OutCapsules, &OutPlanes](const TSharedPtr<FJsonObject>& Container)
        {
            if (!Container.IsValid()) return;
            const TSharedPtr<FJsonObject>* Sphere = nullptr;
            if (Container->TryGetObjectField(TEXT("sphere"), Sphere) && Sphere && Sphere->IsValid())
            {
                FVRMSpringColliderSphere S; 
                S.Offset = ReadVec3(*Sphere, TEXT("offset")); 
                (*Sphere)->TryGetNumberField(TEXT("radius"), S.Radius); 
                if ((*Sphere)->HasTypedField<EJson::Boolean>(TEXT("inside"))) { S.bInside = (*Sphere)->GetBoolField(TEXT("inside")); }
                OutSpheres.Add(S);
            }
            const TSharedPtr<FJsonObject>* Capsule = nullptr;
            if (Container->TryGetObjectField(TEXT("capsule"), Capsule) && Capsule && Capsule->IsValid())
            {
                FVRMSpringColliderCapsule C; 
                C.Offset = ReadVec3(*Capsule, TEXT("offset")); 
                C.TailOffset = ReadVec3(*Capsule, TEXT("tail")); 
                (*Capsule)->TryGetNumberField(TEXT("radius"), C.Radius); 
                if ((*Capsule)->HasTypedField<EJson::Boolean>(TEXT("inside"))) { C.bInside = (*Capsule)->GetBoolField(TEXT("inside")); }
                OutCapsules.Add(C);
            }
            const TSharedPtr<FJsonObject>* Plane = nullptr;
            if (Container->TryGetObjectField(TEXT("plane"), Plane) && Plane && Plane->IsValid())
            {
                FVRMSpringColliderPlane P; 
                P.Offset = ReadVec3(*Plane, TEXT("offset"));
                P.Normal = ReadVec3(*Plane, TEXT("normal"), FVector(0,0,1));
                if (!P.Normal.IsNearlyZero()) P.Normal = P.Normal.GetSafeNormal();
                OutPlanes.Add(P);
            }
        };

        // Direct form
        ParseFromContainer(ShapeEntry);

        // Nested under "shape"
        const TSharedPtr<FJsonObject>* Wrapped = nullptr;
        if (ShapeEntry->TryGetObjectField(TEXT("shape"), Wrapped) && Wrapped && Wrapped->IsValid())
        {
            ParseFromContainer(*Wrapped);
        }

        // Extended collider extension path: extensions.VRMC_springBone_extended_collider.shape
        const TSharedPtr<FJsonObject>* Exts = nullptr;
        if (ShapeEntry->TryGetObjectField(TEXT("extensions"), Exts) && Exts && Exts->IsValid())
        {
            const TSharedPtr<FJsonObject>* ExtCollider = nullptr;
            if ((*Exts)->TryGetObjectField(TEXT("VRMC_springBone_extended_collider"), ExtCollider) && ExtCollider && ExtCollider->IsValid())
            {
                const TSharedPtr<FJsonObject>* ExtShape = nullptr;
                if ((*ExtCollider)->TryGetObjectField(TEXT("shape"), ExtShape) && ExtShape && ExtShape->IsValid())
                {
                    ParseFromContainer(*ExtShape);
                }
            }
        }

        // Some previews of the spec used { "type":"sphere" ... }
        FString Type;
        if (ShapeEntry->TryGetStringField(TEXT("type"), Type))
        {
            Type.TrimStartAndEndInline(); Type.ToLowerInline();
            if (Type == TEXT("sphere"))
            {
                FVRMSpringColliderSphere SphereTemp; 
                SphereTemp.Offset = ReadVec3(ShapeEntry, TEXT("offset")); 
                ShapeEntry->TryGetNumberField(TEXT("radius"), SphereTemp.Radius); 
                if (ShapeEntry->HasTypedField<EJson::Boolean>(TEXT("inside"))) { SphereTemp.bInside = ShapeEntry->GetBoolField(TEXT("inside")); }
                OutSpheres.Add(SphereTemp);
            }
            else if (Type == TEXT("capsule"))
            {
                FVRMSpringColliderCapsule CapsuleTemp; 
                CapsuleTemp.Offset = ReadVec3(ShapeEntry, TEXT("offset")); 
                CapsuleTemp.TailOffset = ReadVec3(ShapeEntry, TEXT("tail")); 
                ShapeEntry->TryGetNumberField(TEXT("radius"), CapsuleTemp.Radius); 
                if (ShapeEntry->HasTypedField<EJson::Boolean>(TEXT("inside"))) { CapsuleTemp.bInside = ShapeEntry->GetBoolField(TEXT("inside")); }
                OutCapsules.Add(CapsuleTemp);
            }
            else if (Type == TEXT("plane"))
            {
                FVRMSpringColliderPlane PlaneTemp; 
                PlaneTemp.Offset = ReadVec3(ShapeEntry, TEXT("offset"));
                PlaneTemp.Normal = ReadVec3(ShapeEntry, TEXT("normal"), FVector(0,0,1));
                if (!PlaneTemp.Normal.IsNearlyZero()) PlaneTemp.Normal = PlaneTemp.Normal.GetSafeNormal();
                OutPlanes.Add(PlaneTemp);
            }
        }
    }

    // Helper: field can be either a number node index or an object { "node": <index> }
    static bool TryGetNodeIndexFlexible(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, int32& OutIndex)
    {
        OutIndex = INDEX_NONE;
        if (!Obj.IsValid()) return false;
        if (Obj->TryGetNumberField(Field, OutIndex))
        {
            return true;
        }
        const TSharedPtr<FJsonObject>* Inner = nullptr;
        if (Obj->TryGetObjectField(Field, Inner) && Inner && Inner->IsValid())
        {
            return (*Inner)->TryGetNumberField(TEXT("node"), OutIndex);
        }
        return false;
    }

    // Build a map from glTF node index -> collider shapes defined by the optional VRMC_node_collider extension.
    // Some exporters store collider shapes on nodes rather than in VRMC_springBone.colliders.
    static void BuildNodeColliderShapeMap(const TSharedPtr<FJsonObject>& Root,
        TMap<int32, TArray<FVRMSpringColliderSphere>>& OutSpheres,
        TMap<int32, TArray<FVRMSpringColliderCapsule>>& OutCapsules,
        TMap<int32, TArray<FVRMSpringColliderPlane>>& OutPlanes)
    {
        OutSpheres.Reset();
        OutCapsules.Reset();
        OutPlanes.Reset();

        if (!Root.IsValid())
        {
            return;
        }

        auto ParseShapesFromObj = [](const TSharedPtr<FJsonObject>& ShapesOwner,
                                     TArray<FVRMSpringColliderSphere>& Spheres,
                                     TArray<FVRMSpringColliderCapsule>& Capsules,
                                     TArray<FVRMSpringColliderPlane>& Planes)
        {
            const TArray<TSharedPtr<FJsonValue>>* Shapes = nullptr;
            if (ShapesOwner.IsValid() && ShapesOwner->TryGetArrayField(TEXT("shapes"), Shapes) && Shapes)
            {
                for (const TSharedPtr<FJsonValue>& SV : *Shapes)
                {
                    const TSharedPtr<FJsonObject>* SObj = nullptr;
                    if (!SV.IsValid() || !SV->TryGetObject(SObj) || !SObj || !SObj->IsValid()) continue;
                    ParseOneShapeObject(*SObj, Spheres, Capsules, Planes);
                }
            }
        };

        // 1) Root-level extension: extensions.VRMC_node_collider.colliders[]
        const TSharedPtr<FJsonObject>* Exts = nullptr;
        if (Root->TryGetObjectField(TEXT("extensions"), Exts) && Exts && Exts->IsValid())
        {
            const TSharedPtr<FJsonObject>* NodeCol = nullptr;
            if ((*Exts)->TryGetObjectField(TEXT("VRMC_node_collider"), NodeCol) && NodeCol && NodeCol->IsValid())
            {
                const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
                if ((*NodeCol)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
                {
                    for (const TSharedPtr<FJsonValue>& CV : *Colliders)
                    {
                        const TSharedPtr<FJsonObject>* CObj = nullptr;
                        if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid()) continue;
                        int32 NodeIndex = INDEX_NONE; (*CObj)->TryGetNumberField(TEXT("node"), NodeIndex);
                        if (NodeIndex == INDEX_NONE) continue;
                        TArray<FVRMSpringColliderSphere>& SArr = OutSpheres.FindOrAdd(NodeIndex);
                        TArray<FVRMSpringColliderCapsule>& CArr = OutCapsules.FindOrAdd(NodeIndex);
                        TArray<FVRMSpringColliderPlane>& PArr = OutPlanes.FindOrAdd(NodeIndex);
                        ParseShapesFromObj(*CObj, SArr, CArr, PArr);
                    }
                }
            }
        }

        // 2) Per-node extension: nodes[i].extensions.VRMC_node_collider.(colliders[]|collider)
        const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
        if (Root->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
        {
            for (int32 NodeIdx = 0; NodeIdx < Nodes->Num(); ++NodeIdx)
            {
                const TSharedPtr<FJsonValue>& NV = (*Nodes)[NodeIdx];
                const TSharedPtr<FJsonObject>* NObj = nullptr;
                if (!NV.IsValid() || !NV->TryGetObject(NObj) || !NObj || !NObj->IsValid()) continue;

                const TSharedPtr<FJsonObject>* NExts = nullptr;
                if (!(*NObj)->TryGetObjectField(TEXT("extensions"), NExts) || !NExts || !NExts->IsValid()) continue;

                const TSharedPtr<FJsonObject>* NodeCol = nullptr;
                if (!(*NExts)->TryGetObjectField(TEXT("VRMC_node_collider"), NodeCol) || !NodeCol || !NodeCol->IsValid()) continue;

                const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
                if ((*NodeCol)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
                {
                    for (const TSharedPtr<FJsonValue>& CV : *Colliders)
                    {
                        const TSharedPtr<FJsonObject>* CObj = nullptr;
                        if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid()) continue;
                        TArray<FVRMSpringColliderSphere>& SArr = OutSpheres.FindOrAdd(NodeIdx);
                        TArray<FVRMSpringColliderCapsule>& CArr = OutCapsules.FindOrAdd(NodeIdx);
                        TArray<FVRMSpringColliderPlane>& PArr = OutPlanes.FindOrAdd(NodeIdx);
                        ParseShapesFromObj(*CObj, SArr, CArr, PArr);
                    }
                }
                else
                {
                    const TSharedPtr<FJsonObject>* Single = nullptr;
                    if ((*NodeCol)->TryGetObjectField(TEXT("collider"), Single) && Single && Single->IsValid())
                    {
                        TArray<FVRMSpringColliderSphere>& SArr = OutSpheres.FindOrAdd(NodeIdx);
                        TArray<FVRMSpringColliderCapsule>& CArr = OutCapsules.FindOrAdd(NodeIdx);
                        TArray<FVRMSpringColliderPlane>& PArr = OutPlanes.FindOrAdd(NodeIdx);
                        ParseShapesFromObj(*Single, SArr, CArr, PArr);
                    }
                }
            }
        }
    }

    // VRM 1.0
    static bool ParseVRM1(const TSharedPtr<FJsonObject>& Root, FVRMSpringConfig& Out, FString& OutError)
    {
        const TSharedPtr<FJsonObject>* Exts = nullptr;
        if (!Root->TryGetObjectField(TEXT("extensions"), Exts) || !Exts || !Exts->IsValid())
        {
            OutError = TEXT("No 'extensions' for VRM1.");
            return false;
        }

        const TSharedPtr<FJsonObject>* Spring = nullptr;
        if (!(*Exts)->TryGetObjectField(TEXT("VRMC_springBone"), Spring) || !Spring || !Spring->IsValid())
        {
            OutError = TEXT("No 'VRMC_springBone' extension.");
            return false;
        }

        Out.Spec = EVRMSpringSpec::VRM1;

        // Fallback map for shapes that may live under VRMC_node_collider
        TMap<int32, TArray<FVRMSpringColliderSphere>> NodeSpheres;
        TMap<int32, TArray<FVRMSpringColliderCapsule>> NodeCapsules;
        TMap<int32, TArray<FVRMSpringColliderPlane>> NodePlanes;
        BuildNodeColliderShapeMap(Root, NodeSpheres, NodeCapsules, NodePlanes);

        // colliders
        const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
        {
            for (const TSharedPtr<FJsonValue>& CV : *Colliders)
            {
                const TSharedPtr<FJsonObject>* CObj = nullptr;
                if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid()) continue;

                FVRMSpringCollider Collider;

                // node refers to glTF node index
                (*CObj)->TryGetNumberField(TEXT("node"), Collider.NodeIndex);

                const TArray<TSharedPtr<FJsonValue>>* Shapes = nullptr;
                if ((*CObj)->TryGetArrayField(TEXT("shapes"), Shapes) && Shapes)
                {
                    for (const TSharedPtr<FJsonValue>& SV : *Shapes)
                    {
                        const TSharedPtr<FJsonObject>* SObj = nullptr;
                        if (!SV.IsValid() || !SV->TryGetObject(SObj) || !SObj || !SObj->IsValid()) continue;

                        // Accept multiple schema variants (now includes plane)
                        ParseOneShapeObject(*SObj, Collider.Spheres, Collider.Capsules, Collider.Planes);
                    }
                }

                // NEW: support single 'shape' object or array when 'shapes' is not present
                if (Collider.Spheres.Num() == 0 && Collider.Capsules.Num() == 0 && Collider.Planes.Num() == 0)
                {
                    const TSharedPtr<FJsonObject>* SingleShapeObj = nullptr;
                    if ((*CObj)->TryGetObjectField(TEXT("shape"), SingleShapeObj) && SingleShapeObj && SingleShapeObj->IsValid())
                    {
                        ParseOneShapeObject(*SingleShapeObj, Collider.Spheres, Collider.Capsules, Collider.Planes);
                    }
                    else
                    {
                        const TArray<TSharedPtr<FJsonValue>>* ShapeArray = nullptr;
                        if ((*CObj)->TryGetArrayField(TEXT("shape"), ShapeArray) && ShapeArray)
                        {
                            for (const TSharedPtr<FJsonValue>& SV : *ShapeArray)
                            {
                                const TSharedPtr<FJsonObject>* SObj = nullptr;
                                if (!SV.IsValid() || !SV->TryGetObject(SObj) || !SObj || !SObj->IsValid()) continue;
                                ParseOneShapeObject(*SObj, Collider.Spheres, Collider.Capsules, Collider.Planes);
                            }
                        }
                    }
                }

                // If shapes were not found in VRMC_springBone, try VRMC_node_collider maps
                if (Collider.Spheres.Num() == 0 && Collider.Capsules.Num() == 0 && Collider.Planes.Num() == 0 && Collider.NodeIndex != INDEX_NONE)
                {
                    if (const TArray<FVRMSpringColliderSphere>* FoundS = NodeSpheres.Find(Collider.NodeIndex))
                    {
                        Collider.Spheres.Append(*FoundS);
                    }
                    if (const TArray<FVRMSpringColliderCapsule>* FoundC = NodeCapsules.Find(Collider.NodeIndex))
                    {
                        Collider.Capsules.Append(*FoundC);
                    }
                    if (const TArray<FVRMSpringColliderPlane>* FoundP = NodePlanes.Find(Collider.NodeIndex))
                    {
                        Collider.Planes.Append(*FoundP);
                    }
                }

                Out.Colliders.Add(MoveTemp(Collider));
            }
        }

        // Synthesized colliders for shapes only defined via node collider extension
        if (Out.Colliders.Num() == 0 && (NodeSpheres.Num() > 0 || NodeCapsules.Num() > 0 || NodePlanes.Num() > 0))
        {
            TSet<int32> NodesWithAnyShape;
            for (const auto& Pair : NodeSpheres) { NodesWithAnyShape.Add(Pair.Key); }
            for (const auto& Pair : NodeCapsules) { NodesWithAnyShape.Add(Pair.Key); }
            for (const auto& Pair : NodePlanes) { NodesWithAnyShape.Add(Pair.Key); }

            for (int32 NodeIdx : NodesWithAnyShape)
            {
                FVRMSpringCollider Synth;
                Synth.NodeIndex = NodeIdx;
                if (const TArray<FVRMSpringColliderSphere>* FoundS = NodeSpheres.Find(NodeIdx)) { Synth.Spheres.Append(*FoundS); }
                if (const TArray<FVRMSpringColliderCapsule>* FoundC = NodeCapsules.Find(NodeIdx)) { Synth.Capsules.Append(*FoundC); }
                if (const TArray<FVRMSpringColliderPlane>* FoundP = NodePlanes.Find(NodeIdx)) { Synth.Planes.Append(*FoundP); }
                if (Synth.Spheres.Num() > 0 || Synth.Capsules.Num() > 0 || Synth.Planes.Num() > 0)
                {
                    UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring Parser] VRM1: Synthesized collider (S=%d C=%d P=%d) for node %d from VRMC_node_collider"), Synth.Spheres.Num(), Synth.Capsules.Num(), Synth.Planes.Num(), NodeIdx);
                    Out.Colliders.Add(MoveTemp(Synth));
                }
            }
        }

        // colliderGroups
        const TArray<TSharedPtr<FJsonValue>>* ColliderGroups = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("colliderGroups"), ColliderGroups) && ColliderGroups)
        {
            for (const TSharedPtr<FJsonValue>& GV : *ColliderGroups)
            {
                const TSharedPtr<FJsonObject>* GObj = nullptr;
                if (!GV.IsValid() || !GV->TryGetObject(GObj) || !GObj || !GObj->IsValid()) continue;

                FVRMSpringColliderGroup Group;
                (*GObj)->TryGetStringField(TEXT("name"), Group.Name);

                const TArray<TSharedPtr<FJsonValue>>* Indices = nullptr;
                if ((*GObj)->TryGetArrayField(TEXT("colliders"), Indices) && Indices)
                {
                    for (const TSharedPtr<FJsonValue>& IV : *Indices)
                    {
                        Group.ColliderIndices.Add((int32)IV->AsNumber());
                    }
                }
                Out.ColliderGroups.Add(MoveTemp(Group));
            }
        }

        // Optional top-level joints array (some exporters place joints here)
        const TArray<TSharedPtr<FJsonValue>>* TopJoints = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("joints"), TopJoints) && TopJoints)
        {
            for (const TSharedPtr<FJsonValue>& JV : *TopJoints)
            {
                const TSharedPtr<FJsonObject>* JObj = nullptr;
                if (!JV.IsValid() || !JV->TryGetObject(JObj) || !JObj || !JObj->IsValid()) continue;

                FVRMSpringJoint J;
                (*JObj)->TryGetNumberField(TEXT("node"), J.NodeIndex);
                (*JObj)->TryGetNumberField(TEXT("hitRadius"), J.HitRadius);
                Out.Joints.Add(MoveTemp(J));
            }
        }

        // springs
        const TArray<TSharedPtr<FJsonValue>>* Springs = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("springs"), Springs) && Springs)
        {
            for (const TSharedPtr<FJsonValue>& SV : *Springs)
            {
                const TSharedPtr<FJsonObject>* SObj = nullptr;
                if (!SV.IsValid() || !SV->TryGetObject(SObj) || !SObj || !SObj->IsValid()) continue;

                FVRMSpring S;
                (*SObj)->TryGetStringField(TEXT("name"), S.Name);

                // center can be a number or an object { node: <index> }
                TryGetNodeIndexFlexible(*SObj, TEXT("center"), S.CenterNodeIndex);

                bool bStiffSet = false, bDragSet = false, bGravPowSet = false, bGravDirSet = false, bHitSet = false;

                float TmpF = 0.f;
                if ((*SObj)->TryGetNumberField(TEXT("stiffness"), TmpF)) { S.Stiffness = TmpF; bStiffSet = true; }
                if ((*SObj)->TryGetNumberField(TEXT("drag"), TmpF)) { S.Drag = TmpF; bDragSet = true; }
                else if ((*SObj)->TryGetNumberField(TEXT("dragForce"), TmpF)) { S.Drag = TmpF; bDragSet = true; }
                if ((*SObj)->TryGetNumberField(TEXT("gravityPower"), TmpF)) { S.GravityPower = TmpF; bGravPowSet = true; }
                {
                    const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
                    if ((*SObj)->TryGetArrayField(TEXT("gravityDir"), Arr) && Arr && Arr->Num() >= 3)
                    {
                        S.GravityDir = ReadVec3(*SObj, TEXT("gravityDir"), FVector(0,-1,0));
                        S.GravityDir = GltfToUE_Dir(S.GravityDir);
                        bGravDirSet = true;
                    }
                }
                if ((*SObj)->TryGetNumberField(TEXT("hitRadius"), TmpF)) { S.HitRadius = TmpF; bHitSet = true; }

                const TArray<TSharedPtr<FJsonValue>>* SJ = nullptr;
                if ((*SObj)->TryGetArrayField(TEXT("joints"), SJ) && SJ)
                {
                    for (const TSharedPtr<FJsonValue>& JV : *SJ)
                    {
                        const TSharedPtr<FJsonObject>* JObj = nullptr;
                        if (JV.IsValid() && JV->TryGetObject(JObj) && JObj && JObj->IsValid())
                        {
                            FVRMSpringJoint J;
                            (*JObj)->TryGetNumberField(TEXT("node"), J.NodeIndex);
                            (*JObj)->TryGetNumberField(TEXT("hitRadius"), J.HitRadius);

                            float JVal = 0.f; bool bAnyAdopted = false;
                            if (!bStiffSet && (*JObj)->TryGetNumberField(TEXT("stiffness"), JVal)) { S.Stiffness = JVal; bStiffSet = true; bAnyAdopted = true; }
                            if (!bDragSet && ( (*JObj)->TryGetNumberField(TEXT("drag"), JVal) || (*JObj)->TryGetNumberField(TEXT("dragForce"), JVal) )) { S.Drag = JVal; bDragSet = true; bAnyAdopted = true; }
                            if (!bGravPowSet && (*JObj)->TryGetNumberField(TEXT("gravityPower"), JVal)) { S.GravityPower = JVal; bGravPowSet = true; bAnyAdopted = true; }
                            if (!bGravDirSet)
                            {
                                const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
                                if ((*JObj)->TryGetArrayField(TEXT("gravityDir"), Arr) && Arr && Arr->Num() >= 3)
                                {
                                    S.GravityDir = ReadVec3(*JObj, TEXT("gravityDir"), FVector(0,-1,0));
                                    S.GravityDir = GltfToUE_Dir(S.GravityDir);
                                    bGravDirSet = true; bAnyAdopted = true;
                                }
                            }
                            if (!bHitSet && (*JObj)->TryGetNumberField(TEXT("hitRadius"), JVal)) { S.HitRadius = JVal; bHitSet = true; bAnyAdopted = true; }

                            if (bAnyAdopted)
                            {
                                UE_LOG(LogVRMSpring, VeryVerbose, TEXT("[VRMSpring Parser] VRM1: Adopted spring params from joint object for spring '%s' (node=%d)"), *S.Name, J.NodeIndex);
                            }

                            const int32 NewJointIndex = Out.Joints.Add(MoveTemp(J));
                            S.JointIndices.Add(NewJointIndex);
                        }
                        else
                        {
                            S.JointIndices.Add((int32)JV->AsNumber());
                        }
                    }
                }

                const TArray<TSharedPtr<FJsonValue>>* CG = nullptr;
                if ((*SObj)->TryGetArrayField(TEXT("colliderGroups"), CG) && CG)
                {
                    for (const TSharedPtr<FJsonValue>& Gv : *CG)
                    {
                        S.ColliderGroupIndices.Add((int32)Gv->AsNumber());
                    }
                }
                Out.Springs.Add(MoveTemp(S));
            }
        }

        return true;
    }

    // VRM 0.x (unchanged, no plane shapes expected)
    static bool ParseVRM0(const TSharedPtr<FJsonObject>& Root, FVRMSpringConfig& Out, FString& OutError)
    {
        const TSharedPtr<FJsonObject>* Exts = nullptr;
        if (!Root->TryGetObjectField(TEXT("extensions"), Exts) || !Exts || !Exts->IsValid())
        {
            OutError = TEXT("No 'extensions' for VRM0.");
            return false;
        }

        const TSharedPtr<FJsonObject>* VrmObj = nullptr;
        if (!(*Exts)->TryGetObjectField(TEXT("VRM"), VrmObj) || !VrmObj || !VrmObj->IsValid())
        {
            OutError = TEXT("No 'VRM' extension.");
            return false;
        }

        const TSharedPtr<FJsonObject>* Sec = nullptr;
        if (!(*VrmObj)->TryGetObjectField(TEXT("secondaryAnimation"), Sec) || !Sec || !Sec->IsValid())
        {
            OutError = TEXT("No 'secondaryAnimation' in VRM 0.x.");
            return false;
        }

        Out.Spec = EVRMSpringSpec::VRM0;

        const TArray<TSharedPtr<FJsonValue>>* ColliderGroups = nullptr;
        TArray<int32> GroupIndexToFirstCollider;
        if ((*Sec)->TryGetArrayField(TEXT("colliderGroups"), ColliderGroups) && ColliderGroups)
        {
            int32 ColliderBase = 0;
            for (const TSharedPtr<FJsonValue>& GV : *ColliderGroups)
            {
                const TSharedPtr<FJsonObject>* GObj = nullptr;
                if (!GV.IsValid() || !GV->TryGetObject(GObj) || !GObj || !GObj->IsValid()) continue;

                int32 NodeIndex = INDEX_NONE;
                (*GObj)->TryGetNumberField(TEXT("node"), NodeIndex);

                FVRMSpringCollider GroupColliderTemplate; // node index propagated to all colliders in the group
                GroupColliderTemplate.NodeIndex = NodeIndex;

                FVRMSpringColliderGroup Group;
                GroupIndexToFirstCollider.Add(ColliderBase);

                const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
                if ((*GObj)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
                {
                    for (const TSharedPtr<FJsonValue>& CV : *Colliders)
                    {
                        const TSharedPtr<FJsonObject>* CObj = nullptr;
                        if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid()) continue;

                        FVRMSpringCollider Collider = GroupColliderTemplate; // copy node index

                        FVRMSpringColliderSphere S;
                        S.Offset = ReadVec3(*CObj, TEXT("offset"));
                        (*CObj)->TryGetNumberField(TEXT("radius"), S.Radius);
                        Collider.Spheres.Add(S);

                        const int32 ThisColliderIndex = Out.Colliders.Num();
                        Group.ColliderIndices.Add(ThisColliderIndex);
                        Out.Colliders.Add(MoveTemp(Collider));
                        ColliderBase++;
                    }
                }
                Out.ColliderGroups.Add(MoveTemp(Group));
            }
        }

        const TArray<TSharedPtr<FJsonValue>>* BoneGroups = nullptr;
        if ((*Sec)->TryGetArrayField(TEXT("boneGroups"), BoneGroups) && BoneGroups)
        {
            for (const TSharedPtr<FJsonValue>& BV : *BoneGroups)
            {
                const TSharedPtr<FJsonObject>* BObj = nullptr;
                if (!BV.IsValid() || !BV->TryGetObject(BObj) || !BObj || !BObj->IsValid()) continue;

                FVRMSpring Spring;
                (*BObj)->TryGetStringField(TEXT("comment"), Spring.Name);
                (*BObj)->TryGetNumberField(TEXT("center"), Spring.CenterNodeIndex);

                bool bUsedLegacyStiffiness = false;
                if ((*BObj)->TryGetNumberField(TEXT("stiffiness"), Spring.Stiffness))
                {
                    bUsedLegacyStiffiness = true;
                    UE_LOG(LogVRMSpring, Verbose, TEXT("[VRMSpring Parser] VRM0: detected legacy 'stiffiness' field and mapped to 'stiffness' (value=%.3f)"), Spring.Stiffness);
                }
                (*BObj)->TryGetNumberField(TEXT("stiffness"), Spring.Stiffness);
                (*BObj)->TryGetNumberField(TEXT("dragForce"), Spring.Drag);
                Spring.GravityDir = ReadVec3(*BObj, TEXT("gravityDir"), FVector(0, -1, 0));
                Spring.GravityDir = GltfToUE_Dir(Spring.GravityDir);
                (*BObj)->TryGetNumberField(TEXT("gravityPower"), Spring.GravityPower);
                (*BObj)->TryGetNumberField(TEXT("hitRadius"), Spring.HitRadius);

                if (bUsedLegacyStiffiness)
                {
                    UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring Parser] VRM0: Mapped legacy 'stiffiness' to 'stiffness' for spring '%s'"), *Spring.Name);
                }

                const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
                if ((*BObj)->TryGetArrayField(TEXT("bones"), Bones) && Bones)
                {
                    for (const TSharedPtr<FJsonValue>& BVV : *Bones)
                    {
                        FVRMSpringJoint J;
                        J.NodeIndex = (int32)BVV->AsNumber();
                        const int32 JIndex = Out.Joints.Add(J);
                        Spring.JointIndices.Add(JIndex);
                    }
                }

                const TArray<TSharedPtr<FJsonValue>>* CG = nullptr;
                if ((*BObj)->TryGetArrayField(TEXT("colliderGroups"), CG) && CG)
                {
                    for (const TSharedPtr<FJsonValue>& Gv : *CG)
                    {
                        Spring.ColliderGroupIndices.Add((int32)Gv->AsNumber());
                    }
                }

                Out.Springs.Add(MoveTemp(Spring));
            }
        }

        return true;
    }
}

namespace VRM
{
    bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, FString& OutError)
    {
        OutConfig = FVRMSpringConfig();
        OutError.Empty();
        if (Json.IsEmpty()) { OutError = TEXT("Empty JSON."); return false; }
        TSharedPtr<FJsonObject> Root; const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) { OutError = TEXT("Failed to parse JSON."); return false; }
        if (ParseVRM1(Root, OutConfig, OutError)) { OutConfig.RawJson = Json; UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring Parser] Parsed VRM spring bones as VRM1: Springs=%d Colliders=%d Joints=%d ColliderGroups=%d"), OutConfig.Springs.Num(), OutConfig.Colliders.Num(), OutConfig.Joints.Num(), OutConfig.ColliderGroups.Num()); return true; }
        FString Err0; FVRMSpringConfig As0; if (ParseVRM0(Root, As0, Err0)) { OutConfig = MoveTemp(As0); OutConfig.RawJson = Json; OutError.Reset(); UE_LOG(LogVRMSpring, Log, TEXT("[VRMSpring Parser] Parsed VRM spring bones as VRM0: Springs=%d Colliders=%d Joints=%d ColliderGroups=%d"), OutConfig.Springs.Num(), OutConfig.Colliders.Num(), OutConfig.Joints.Num(), OutConfig.ColliderGroups.Num()); return true; }
        OutError = TEXT("No VRM spring bone data detected."); return false;
    }
    bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, FString& OutError)
    {
        FString Json; if (!ExtractTopLevelJsonString(Filename, Json)) { OutError = TEXT("Could not extract top-level JSON from file."); return false; } return ParseSpringBonesFromJson(Json, OutConfig, OutError);
    }

    bool ParseSpringBonesFromJson(const FString& Json, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError)
    {
        OutNodeMap.Reset();
        if (!ParseSpringBonesFromJson(Json, OutConfig, OutError))
        {
            return false;
        }
        // Extract node names for mapping
        TSharedPtr<FJsonObject> Root; const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* Nodes = nullptr;
            if (Root->TryGetArrayField(TEXT("nodes"), Nodes) && Nodes)
            {
                for (int32 i = 0; i < Nodes->Num(); ++i)
                {
                    const TSharedPtr<FJsonValue>& V = (*Nodes)[i];
                    const TSharedPtr<FJsonObject>* NObj = nullptr;
                    if (V.IsValid() && V->TryGetObject(NObj) && NObj && NObj->IsValid())
                    {
                        FString NameStr;
                        if ((*NObj)->TryGetStringField(TEXT("name"), NameStr) && !NameStr.IsEmpty())
                        {
                            OutNodeMap.Add(i, FName(*NameStr));
                        }
                    }
                }
            }
        }
        return true;
    }

    bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, TMap<int32, FName>& OutNodeMap, FString& OutError)
    {
        FString Json;
        if (!ExtractTopLevelJsonString(Filename, Json))
        {
            OutError = TEXT("Could not extract top-level JSON from file.");
            return false;
        }
        return ParseSpringBonesFromJson(Json, OutConfig, OutNodeMap, OutError);
    }
}