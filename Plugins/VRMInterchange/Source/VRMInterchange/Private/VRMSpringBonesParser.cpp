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

                        // sphere
                        const TSharedPtr<FJsonObject>* Sphere = nullptr;
                        if ((*SObj)->TryGetObjectField(TEXT("sphere"), Sphere) && Sphere && Sphere->IsValid())
                        {
                            FVRMSpringColliderSphere S;
                            S.Offset = ReadVec3(*Sphere, TEXT("offset"));
                            (*Sphere)->TryGetNumberField(TEXT("radius"), S.Radius);
                            Collider.Spheres.Add(S);
                        }

                        // capsule
                        const TSharedPtr<FJsonObject>* Capsule = nullptr;
                        if ((*SObj)->TryGetObjectField(TEXT("capsule"), Capsule) && Capsule && Capsule->IsValid())
                        {
                            FVRMSpringColliderCapsule C;
                            C.Offset = ReadVec3(*Capsule, TEXT("offset"));
                            C.TailOffset = ReadVec3(*Capsule, TEXT("tail"));
                            (*Capsule)->TryGetNumberField(TEXT("radius"), C.Radius);
                            Collider.Capsules.Add(C);
                        }
                    }
                }

                Out.Colliders.Add(MoveTemp(Collider));
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

        // joints
        const TArray<TSharedPtr<FJsonValue>>* Joints = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("joints"), Joints) && Joints)
        {
            for (const TSharedPtr<FJsonValue>& JV : *Joints)
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
                (*SObj)->TryGetNumberField(TEXT("center"), S.CenterNodeIndex);
                (*SObj)->TryGetNumberField(TEXT("stiffness"), S.Stiffness);
                (*SObj)->TryGetNumberField(TEXT("drag"), S.Drag);
                S.GravityDir = ReadVec3(*SObj, TEXT("gravityDir"), FVector(0, 0, -1));
                (*SObj)->TryGetNumberField(TEXT("gravityPower"), S.GravityPower);
                (*SObj)->TryGetNumberField(TEXT("hitRadius"), S.HitRadius);

                const TArray<TSharedPtr<FJsonValue>>* SJ = nullptr;
                if ((*SObj)->TryGetArrayField(TEXT("joints"), SJ) && SJ)
                {
                    for (const TSharedPtr<FJsonValue>& JV : *SJ)
                    {
                        S.JointIndices.Add((int32)JV->AsNumber());
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

    // VRM 0.x
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

        // colliders: flattened from colliderGroups[].colliders (spheres only in 0.x)
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

                FVRMSpringColliderGroup Group;
                GroupIndexToFirstCollider.Add(ColliderBase);

                const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
                if ((*GObj)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
                {
                    for (const TSharedPtr<FJsonValue>& CV : *Colliders)
                    {
                        const TSharedPtr<FJsonObject>* CObj = nullptr;
                        if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid()) continue;

                        FVRMSpringCollider Collider;
                        Collider.NodeIndex = NodeIndex;

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

        // boneGroups -> springs
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
                (*BObj)->TryGetNumberField(TEXT("stiffiness"), Spring.Stiffness); // some files use 'stiffiness' (typo)
                (*BObj)->TryGetNumberField(TEXT("stiffness"), Spring.Stiffness);
                (*BObj)->TryGetNumberField(TEXT("dragForce"), Spring.Drag);
                Spring.GravityDir = ReadVec3(*BObj, TEXT("gravityDir"), FVector(0, 0, -1));
                (*BObj)->TryGetNumberField(TEXT("gravityPower"), Spring.GravityPower);
                (*BObj)->TryGetNumberField(TEXT("hitRadius"), Spring.HitRadius);

                // bones -> we map to Joints + indices
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

                // colliderGroups indices
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

        if (Json.IsEmpty())
        {
            OutError = TEXT("Empty JSON.");
            return false;
        }

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            OutError = TEXT("Failed to parse JSON.");
            return false;
        }

        // Try VRM1 first
        if (ParseVRM1(Root, OutConfig, OutError))
        {
            OutConfig.RawJson = Json;
            return true;
        }

        // Reset error, try VRM0
        FString Err0;
        FVRMSpringConfig As0;
        if (ParseVRM0(Root, As0, Err0))
        {
            OutConfig = MoveTemp(As0);
            OutConfig.RawJson = Json;
            OutError.Reset();
            return true;
        }

        // No spring data
        OutError = TEXT("No VRM spring bone data detected.");
        return false;
    }

    bool ParseSpringBonesFromFile(const FString& Filename, FVRMSpringConfig& OutConfig, FString& OutError)
    {
        FString Json;
        if (!ExtractTopLevelJsonString(Filename, Json))
        {
            OutError = TEXT("Could not extract top-level JSON from file.");
            return false;
        }
        return ParseSpringBonesFromJson(Json, OutConfig, OutError);
    }
}