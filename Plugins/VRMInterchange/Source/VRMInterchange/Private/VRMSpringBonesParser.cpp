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

        // Check for optional specVersion field
        FString SpecVersion;
        if ((*Spring)->TryGetStringField(TEXT("specVersion"), SpecVersion))
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("VRM 1.0 specVersion: %s"), *SpecVersion);
        }
        else
        {
            Out.ParseWarnings.Add(TEXT("Missing specVersion field in VRMC_springBone"));
        }

        // colliders
        const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("Parsing %d VRM 1.0 colliders"), Colliders->Num());
            for (int32 ColliderIndex = 0; ColliderIndex < Colliders->Num(); ++ColliderIndex)
            {
                const TSharedPtr<FJsonValue>& CV = (*Colliders)[ColliderIndex];
                const TSharedPtr<FJsonObject>* CObj = nullptr;
                if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid())
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Invalid collider object at index %d"), ColliderIndex));
                    continue;
                }

                FVRMSpringCollider Collider;

                // node refers to glTF node index
                if (!(*CObj)->TryGetNumberField(TEXT("node"), Collider.NodeIndex))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Collider %d missing required 'node' field"), ColliderIndex));
                }

                const TArray<TSharedPtr<FJsonValue>>* Shapes = nullptr;
                if ((*CObj)->TryGetArrayField(TEXT("shapes"), Shapes) && Shapes)
                {
                    if (Shapes->Num() == 0)
                    {
                        Out.ParseWarnings.Add(FString::Printf(TEXT("Collider %d has empty shapes array"), ColliderIndex));
                    }

                    for (int32 ShapeIndex = 0; ShapeIndex < Shapes->Num(); ++ShapeIndex)
                    {
                        const TSharedPtr<FJsonValue>& SV = (*Shapes)[ShapeIndex];
                        const TSharedPtr<FJsonObject>* SObj = nullptr;
                        if (!SV.IsValid() || !SV->TryGetObject(SObj) || !SObj || !SObj->IsValid())
                        {
                            Out.ParseWarnings.Add(FString::Printf(TEXT("Invalid shape object at collider %d, shape %d"), ColliderIndex, ShapeIndex));
                            continue;
                        }

                        // sphere
                        const TSharedPtr<FJsonObject>* Sphere = nullptr;
                        if ((*SObj)->TryGetObjectField(TEXT("sphere"), Sphere) && Sphere && Sphere->IsValid())
                        {
                            FVRMSpringColliderSphere S;
                            S.Offset = ReadVec3(*Sphere, TEXT("offset"));
                            if (!(*Sphere)->TryGetNumberField(TEXT("radius"), S.Radius))
                            {
                                Out.ParseWarnings.Add(FString::Printf(TEXT("Sphere at collider %d, shape %d missing 'radius'"), ColliderIndex, ShapeIndex));
                            }
                            if (S.Radius <= 0.0f)
                            {
                                Out.ParseWarnings.Add(FString::Printf(TEXT("Sphere at collider %d, shape %d has invalid radius %f"), ColliderIndex, ShapeIndex, S.Radius));
                            }
                            Collider.Spheres.Add(S);
                        }

                        // capsule
                        const TSharedPtr<FJsonObject>* Capsule = nullptr;
                        if ((*SObj)->TryGetObjectField(TEXT("capsule"), Capsule) && Capsule && Capsule->IsValid())
                        {
                            FVRMSpringColliderCapsule C;
                            C.Offset = ReadVec3(*Capsule, TEXT("offset"));
                            C.TailOffset = ReadVec3(*Capsule, TEXT("tail"));
                            if (!(*Capsule)->TryGetNumberField(TEXT("radius"), C.Radius))
                            {
                                Out.ParseWarnings.Add(FString::Printf(TEXT("Capsule at collider %d, shape %d missing 'radius'"), ColliderIndex, ShapeIndex));
                            }
                            if (C.Radius <= 0.0f)
                            {
                                Out.ParseWarnings.Add(FString::Printf(TEXT("Capsule at collider %d, shape %d has invalid radius %f"), ColliderIndex, ShapeIndex, C.Radius));
                            }
                            Collider.Capsules.Add(C);
                        }

                        // Check for unsupported shapes
                        TArray<FString> Keys;
                        (*SObj)->Values.GetKeys(Keys);
                        for (const FString& Key : Keys)
                        {
                            if (Key != TEXT("sphere") && Key != TEXT("capsule"))
                            {
                                Out.UnsupportedFeatures.Add(FString::Printf(TEXT("Unsupported collider shape type '%s' at collider %d, shape %d"), *Key, ColliderIndex, ShapeIndex));
                            }
                        }
                    }
                }
                else
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Collider %d missing 'shapes' array"), ColliderIndex));
                }

                if (Collider.Spheres.Num() == 0 && Collider.Capsules.Num() == 0)
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Collider %d has no valid shapes"), ColliderIndex));
                }

                Out.Colliders.Add(MoveTemp(Collider));
            }
        }
        else
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("No colliders array found in VRM 1.0 data"));
        }

        // colliderGroups
        const TArray<TSharedPtr<FJsonValue>>* ColliderGroups = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("colliderGroups"), ColliderGroups) && ColliderGroups)
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("Parsing %d VRM 1.0 collider groups"), ColliderGroups->Num());
            for (int32 GroupIndex = 0; GroupIndex < ColliderGroups->Num(); ++GroupIndex)
            {
                const TSharedPtr<FJsonValue>& GV = (*ColliderGroups)[GroupIndex];
                const TSharedPtr<FJsonObject>* GObj = nullptr;
                if (!GV.IsValid() || !GV->TryGetObject(GObj) || !GObj || !GObj->IsValid())
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Invalid collider group object at index %d"), GroupIndex));
                    continue;
                }

                FVRMSpringColliderGroup Group;
                if (!(*GObj)->TryGetStringField(TEXT("name"), Group.Name))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Collider group %d missing 'name' field"), GroupIndex));
                    Group.Name = FString::Printf(TEXT("Group_%d"), GroupIndex);
                }

                const TArray<TSharedPtr<FJsonValue>>* Indices = nullptr;
                if ((*GObj)->TryGetArrayField(TEXT("colliders"), Indices) && Indices)
                {
                    for (int32 RefIndex = 0; RefIndex < Indices->Num(); ++RefIndex)
                    {
                        const TSharedPtr<FJsonValue>& IV = (*Indices)[RefIndex];
                        int32 ColliderIndex = (int32)IV->AsNumber();
                        if (ColliderIndex < 0 || ColliderIndex >= Out.Colliders.Num())
                        {
                            Out.ParseWarnings.Add(FString::Printf(TEXT("Collider group %d references invalid collider index %d (valid range: 0-%d)"), 
                                GroupIndex, ColliderIndex, Out.Colliders.Num() - 1));
                        }
                        Group.ColliderIndices.Add(ColliderIndex);
                    }
                }
                else
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Collider group %d missing 'colliders' array"), GroupIndex));
                }

                Out.ColliderGroups.Add(MoveTemp(Group));
            }
        }
        else
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("No collider groups array found in VRM 1.0 data"));
        }

        // Optional top-level joints array (some exporters place joints here)
        const TArray<TSharedPtr<FJsonValue>>* TopJoints = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("joints"), TopJoints) && TopJoints)
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("Parsing %d VRM 1.0 top-level joints"), TopJoints->Num());
            for (int32 JointIndex = 0; JointIndex < TopJoints->Num(); ++JointIndex)
            {
                const TSharedPtr<FJsonValue>& JV = (*TopJoints)[JointIndex];
                const TSharedPtr<FJsonObject>* JObj = nullptr;
                if (!JV.IsValid() || !JV->TryGetObject(JObj) || !JObj || !JObj->IsValid())
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Invalid joint object at index %d"), JointIndex));
                    continue;
                }

                FVRMSpringJoint J;
                if (!(*JObj)->TryGetNumberField(TEXT("node"), J.NodeIndex))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Joint %d missing required 'node' field"), JointIndex));
                }
                if (!(*JObj)->TryGetNumberField(TEXT("hitRadius"), J.HitRadius))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Joint %d missing 'hitRadius', using default 0.0"), JointIndex));
                }
                if (J.HitRadius < 0.0f)
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Joint %d has negative hitRadius %f, clamping to 0.0"), JointIndex, J.HitRadius));
                    J.HitRadius = 0.0f;
                }
                Out.Joints.Add(MoveTemp(J));
            }
        }

        // springs
        const TArray<TSharedPtr<FJsonValue>>* Springs = nullptr;
        if ((*Spring)->TryGetArrayField(TEXT("springs"), Springs) && Springs)
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("Parsing %d VRM 1.0 springs"), Springs->Num());
            for (int32 SpringIndex = 0; SpringIndex < Springs->Num(); ++SpringIndex)
            {
                const TSharedPtr<FJsonValue>& SV = (*Springs)[SpringIndex];
                const TSharedPtr<FJsonObject>* SObj = nullptr;
                if (!SV.IsValid() || !SV->TryGetObject(SObj) || !SObj || !SObj->IsValid())
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Invalid spring object at index %d"), SpringIndex));
                    continue;
                }

                FVRMSpring S;
                if (!(*SObj)->TryGetStringField(TEXT("name"), S.Name))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d missing 'name' field"), SpringIndex));
                    S.Name = FString::Printf(TEXT("Spring_%d"), SpringIndex);
                }

                // center can be a number or an object { node: <index> }
                if (!TryGetNodeIndexFlexible(*SObj, TEXT("center"), S.CenterNodeIndex))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) missing or invalid 'center' field"), SpringIndex, *S.Name));
                }

                // Spring-level parameters are optional in VRM1 (often per-joint), keep parsing if present
                if (!(*SObj)->TryGetNumberField(TEXT("stiffness"), S.Stiffness))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) missing 'stiffness', using default 0.0"), SpringIndex, *S.Name));
                }
                if (S.Stiffness < 0.0f || S.Stiffness > 1.0f)
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) has stiffness %f outside typical range [0.0, 1.0]"), SpringIndex, *S.Name, S.Stiffness));
                }

                if (!(*SObj)->TryGetNumberField(TEXT("drag"), S.Drag))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) missing 'drag', using default 0.0"), SpringIndex, *S.Name));
                }
                if (S.Drag < 0.0f || S.Drag > 1.0f)
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) has drag %f outside typical range [0.0, 1.0]"), SpringIndex, *S.Name, S.Drag));
                }

                S.GravityDir = ReadVec3(*SObj, TEXT("gravityDir"), FVector(0, 0, -1));
                if (FMath::IsNearlyZero(S.GravityDir.SizeSquared()))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) has zero-length gravity direction, using default (0,0,-1)"), SpringIndex, *S.Name));
                    S.GravityDir = FVector(0, 0, -1);
                }

                if (!(*SObj)->TryGetNumberField(TEXT("gravityPower"), S.GravityPower))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) missing 'gravityPower', using default 0.0"), SpringIndex, *S.Name));
                }

                if (!(*SObj)->TryGetNumberField(TEXT("hitRadius"), S.HitRadius))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) missing 'hitRadius', using default 0.0"), SpringIndex, *S.Name));
                }
                if (S.HitRadius < 0.0f)
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) has negative hitRadius %f, clamping to 0.0"), SpringIndex, *S.Name, S.HitRadius));
                    S.HitRadius = 0.0f;
                }

                const TArray<TSharedPtr<FJsonValue>>* SJ = nullptr;
                if ((*SObj)->TryGetArrayField(TEXT("joints"), SJ) && SJ)
                {
                    for (int32 JointRefIndex = 0; JointRefIndex < SJ->Num(); ++JointRefIndex)
                    {
                        const TSharedPtr<FJsonValue>& JV = (*SJ)[JointRefIndex];
                        // joints entry can be a number (index into top-level joints) or an object with node and params
                        const TSharedPtr<FJsonObject>* JObj = nullptr;
                        if (JV.IsValid() && JV->TryGetObject(JObj) && JObj && JObj->IsValid())
                        {
                            // Create a joint entry based on this object
                            FVRMSpringJoint J;
                            if (!(*JObj)->TryGetNumberField(TEXT("node"), J.NodeIndex))
                            {
                                Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) joint %d missing 'node' field"), SpringIndex, *S.Name, JointRefIndex));
                            }
                            if (!(*JObj)->TryGetNumberField(TEXT("hitRadius"), J.HitRadius))
                            {
                                Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) joint %d missing 'hitRadius'"), SpringIndex, *S.Name, JointRefIndex));
                            }
                            if (J.HitRadius < 0.0f)
                            {
                                Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) joint %d has negative hitRadius %f, clamping to 0.0"), SpringIndex, *S.Name, JointRefIndex, J.HitRadius));
                                J.HitRadius = 0.0f;
                            }

                            const int32 NewJointIndex = Out.Joints.Add(MoveTemp(J));
                            S.JointIndices.Add(NewJointIndex);
                        }
                        else
                        {
                            // Fallback: assume it's a numeric index
                            int32 JointIndex = (int32)JV->AsNumber();
                            if (JointIndex < 0 || JointIndex >= Out.Joints.Num())
                            {
                                Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) references invalid joint index %d (valid range: 0-%d)"), 
                                    SpringIndex, *S.Name, JointIndex, Out.Joints.Num() - 1));
                            }
                            S.JointIndices.Add(JointIndex);
                        }
                    }
                }
                else
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) missing 'joints' array"), SpringIndex, *S.Name));
                }

                const TArray<TSharedPtr<FJsonValue>>* CG = nullptr;
                if ((*SObj)->TryGetArrayField(TEXT("colliderGroups"), CG) && CG)
                {
                    for (int32 ColliderGroupRefIndex = 0; ColliderGroupRefIndex < CG->Num(); ++ColliderGroupRefIndex)
                    {
                        const TSharedPtr<FJsonValue>& Gv = (*CG)[ColliderGroupRefIndex];
                        int32 GroupIndex = (int32)Gv->AsNumber();
                        if (GroupIndex < 0 || GroupIndex >= Out.ColliderGroups.Num())
                        {
                            Out.ParseWarnings.Add(FString::Printf(TEXT("Spring %d (%s) references invalid collider group index %d (valid range: 0-%d)"), 
                                SpringIndex, *S.Name, GroupIndex, Out.ColliderGroups.Num() - 1));
                        }
                        S.ColliderGroupIndices.Add(GroupIndex);
                    }
                }
                else
                {
                    UE_LOG(LogVRMSpring, Verbose, TEXT("Spring %d (%s) has no collider groups"), SpringIndex, *S.Name);
                }

                Out.Springs.Add(MoveTemp(S));
            }
        }
        else
        {
            Out.ParseWarnings.Add(TEXT("No 'springs' array found in VRM 1.0 data"));
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

        // Check for optional specVersion field in VRM 0.x 
        FString SpecVersion;
        if ((*VrmObj)->TryGetStringField(TEXT("specVersion"), SpecVersion))
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("VRM 0.x specVersion: %s"), *SpecVersion);
        }
        else
        {
            Out.ParseWarnings.Add(TEXT("Missing specVersion field in VRM extension"));
        }

        // colliders: flattened from colliderGroups[].colliders (spheres only in 0.x)
        const TArray<TSharedPtr<FJsonValue>>* ColliderGroups = nullptr;
        TArray<int32> GroupIndexToFirstCollider;
        if ((*Sec)->TryGetArrayField(TEXT("colliderGroups"), ColliderGroups) && ColliderGroups)
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("Parsing %d VRM 0.x collider groups"), ColliderGroups->Num());
            int32 ColliderBase = 0;
            for (int32 GroupIndex = 0; GroupIndex < ColliderGroups->Num(); ++GroupIndex)
            {
                const TSharedPtr<FJsonValue>& GV = (*ColliderGroups)[GroupIndex];
                const TSharedPtr<FJsonObject>* GObj = nullptr;
                if (!GV.IsValid() || !GV->TryGetObject(GObj) || !GObj || !GObj->IsValid())
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Invalid collider group object at index %d"), GroupIndex));
                    continue;
                }

                int32 NodeIndex = INDEX_NONE;
                if (!(*GObj)->TryGetNumberField(TEXT("node"), NodeIndex))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x collider group %d missing 'node' field"), GroupIndex));
                }

                FVRMSpringColliderGroup Group;
                Group.Name = FString::Printf(TEXT("Group_%d"), GroupIndex); // VRM 0.x doesn't have explicit names
                GroupIndexToFirstCollider.Add(ColliderBase);

                const TArray<TSharedPtr<FJsonValue>>* Colliders = nullptr;
                if ((*GObj)->TryGetArrayField(TEXT("colliders"), Colliders) && Colliders)
                {
                    if (Colliders->Num() == 0)
                    {
                        Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x collider group %d has empty colliders array"), GroupIndex));
                    }

                    for (int32 ColliderIndex = 0; ColliderIndex < Colliders->Num(); ++ColliderIndex)
                    {
                        const TSharedPtr<FJsonValue>& CV = (*Colliders)[ColliderIndex];
                        const TSharedPtr<FJsonObject>* CObj = nullptr;
                        if (!CV.IsValid() || !CV->TryGetObject(CObj) || !CObj || !CObj->IsValid())
                        {
                            Out.ParseWarnings.Add(FString::Printf(TEXT("Invalid collider object at group %d, collider %d"), GroupIndex, ColliderIndex));
                            continue;
                        }

                        FVRMSpringCollider Collider;
                        Collider.NodeIndex = NodeIndex;

                        FVRMSpringColliderSphere S;
                        S.Offset = ReadVec3(*CObj, TEXT("offset"));
                        if (!(*CObj)->TryGetNumberField(TEXT("radius"), S.Radius))
                        {
                            Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x collider at group %d, collider %d missing 'radius'"), GroupIndex, ColliderIndex));
                        }
                        if (S.Radius <= 0.0f)
                        {
                            Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x collider at group %d, collider %d has invalid radius %f"), GroupIndex, ColliderIndex, S.Radius));
                        }

                        // VRM 0.x only supports spheres - warn if other shape fields are found
                        TArray<FString> Keys;
                        (*CObj)->Values.GetKeys(Keys);
                        for (const FString& Key : Keys)
                        {
                            if (Key != TEXT("offset") && Key != TEXT("radius"))
                            {
                                Out.UnsupportedFeatures.Add(FString::Printf(TEXT("VRM 0.x collider has unsupported field '%s' at group %d, collider %d (VRM 0.x only supports spheres)"), *Key, GroupIndex, ColliderIndex));
                            }
                        }

                        Collider.Spheres.Add(S);

                        const int32 ThisColliderIndex = Out.Colliders.Num();
                        Group.ColliderIndices.Add(ThisColliderIndex);
                        Out.Colliders.Add(MoveTemp(Collider));
                        ColliderBase++;
                    }
                }
                else
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x collider group %d missing 'colliders' array"), GroupIndex));
                }
                Out.ColliderGroups.Add(MoveTemp(Group));
            }
        }
        else
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("No collider groups array found in VRM 0.x data"));
        }

        // boneGroups -> springs
        const TArray<TSharedPtr<FJsonValue>>* BoneGroups = nullptr;
        if ((*Sec)->TryGetArrayField(TEXT("boneGroups"), BoneGroups) && BoneGroups)
        {
            UE_LOG(LogVRMSpring, Verbose, TEXT("Parsing %d VRM 0.x bone groups (springs)"), BoneGroups->Num());
            for (int32 GroupIndex = 0; GroupIndex < BoneGroups->Num(); ++GroupIndex)
            {
                const TSharedPtr<FJsonValue>& BV = (*BoneGroups)[GroupIndex];
                const TSharedPtr<FJsonObject>* BObj = nullptr;
                if (!BV.IsValid() || !BV->TryGetObject(BObj) || !BObj || !BObj->IsValid())
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("Invalid bone group object at index %d"), GroupIndex));
                    continue;
                }

                FVRMSpring Spring;
                if (!(*BObj)->TryGetStringField(TEXT("comment"), Spring.Name))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d missing 'comment' field"), GroupIndex));
                    Spring.Name = FString::Printf(TEXT("BoneGroup_%d"), GroupIndex);
                }

                if (!(*BObj)->TryGetNumberField(TEXT("center"), Spring.CenterNodeIndex))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) missing 'center' field"), GroupIndex, *Spring.Name));
                }

                // VRM 0.x supports both 'stiffness' and the typo 'stiffiness'
                bool bFoundStiffness = false;
                if ((*BObj)->TryGetNumberField(TEXT("stiffiness"), Spring.Stiffness)) // typo variant
                {
                    bFoundStiffness = true;
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) uses deprecated 'stiffiness' field (typo for 'stiffness')"), GroupIndex, *Spring.Name));
                }
                if ((*BObj)->TryGetNumberField(TEXT("stiffness"), Spring.Stiffness)) // correct spelling takes precedence
                {
                    bFoundStiffness = true;
                }
                if (!bFoundStiffness)
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) missing 'stiffness' field"), GroupIndex, *Spring.Name));
                }
                if (Spring.Stiffness < 0.0f || Spring.Stiffness > 1.0f)
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) has stiffness %f outside typical range [0.0, 1.0]"), GroupIndex, *Spring.Name, Spring.Stiffness));
                }

                if (!(*BObj)->TryGetNumberField(TEXT("dragForce"), Spring.Drag))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) missing 'dragForce' field"), GroupIndex, *Spring.Name));
                }
                if (Spring.Drag < 0.0f || Spring.Drag > 1.0f)
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) has dragForce %f outside typical range [0.0, 1.0]"), GroupIndex, *Spring.Name, Spring.Drag));
                }

                Spring.GravityDir = ReadVec3(*BObj, TEXT("gravityDir"), FVector(0, 0, -1));
                if (FMath::IsNearlyZero(Spring.GravityDir.SizeSquared()))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) has zero-length gravity direction, using default (0,0,-1)"), GroupIndex, *Spring.Name));
                    Spring.GravityDir = FVector(0, 0, -1);
                }

                if (!(*BObj)->TryGetNumberField(TEXT("gravityPower"), Spring.GravityPower))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) missing 'gravityPower' field"), GroupIndex, *Spring.Name));
                }

                if (!(*BObj)->TryGetNumberField(TEXT("hitRadius"), Spring.HitRadius))
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) missing 'hitRadius' field"), GroupIndex, *Spring.Name));
                }
                if (Spring.HitRadius < 0.0f)
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) has negative hitRadius %f, clamping to 0.0"), GroupIndex, *Spring.Name, Spring.HitRadius));
                    Spring.HitRadius = 0.0f;
                }

                // bones -> we map to Joints + indices
                const TArray<TSharedPtr<FJsonValue>>* Bones = nullptr;
                if ((*BObj)->TryGetArrayField(TEXT("bones"), Bones) && Bones)
                {
                    if (Bones->Num() == 0)
                    {
                        Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) has empty bones array"), GroupIndex, *Spring.Name));
                    }

                    for (int32 BoneIndex = 0; BoneIndex < Bones->Num(); ++BoneIndex)
                    {
                        const TSharedPtr<FJsonValue>& BVV = (*Bones)[BoneIndex];
                        FVRMSpringJoint J;
                        J.NodeIndex = (int32)BVV->AsNumber();
                        if (J.NodeIndex < 0)
                        {
                            Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) bone %d has invalid node index %d"), GroupIndex, *Spring.Name, BoneIndex, J.NodeIndex));
                        }
                        // In VRM 0.x, hitRadius is at the spring level, not per-joint
                        J.HitRadius = Spring.HitRadius;
                        const int32 JIndex = Out.Joints.Add(J);
                        Spring.JointIndices.Add(JIndex);
                    }
                }
                else
                {
                    Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) missing 'bones' array"), GroupIndex, *Spring.Name));
                }

                // colliderGroups indices
                const TArray<TSharedPtr<FJsonValue>>* CG = nullptr;
                if ((*BObj)->TryGetArrayField(TEXT("colliderGroups"), CG) && CG)
                {
                    for (int32 GroupRefIndex = 0; GroupRefIndex < CG->Num(); ++GroupRefIndex)
                    {
                        const TSharedPtr<FJsonValue>& Gv = (*CG)[GroupRefIndex];
                        int32 ColliderGroupIndex = (int32)Gv->AsNumber();
                        if (ColliderGroupIndex < 0 || ColliderGroupIndex >= Out.ColliderGroups.Num())
                        {
                            Out.ParseWarnings.Add(FString::Printf(TEXT("VRM 0.x bone group %d (%s) references invalid collider group index %d (valid range: 0-%d)"), 
                                GroupIndex, *Spring.Name, ColliderGroupIndex, Out.ColliderGroups.Num() - 1));
                        }
                        Spring.ColliderGroupIndices.Add(ColliderGroupIndex);
                    }
                }
                else
                {
                    UE_LOG(LogVRMSpring, Verbose, TEXT("VRM 0.x bone group %d (%s) has no collider groups"), GroupIndex, *Spring.Name);
                }

                Out.Springs.Add(MoveTemp(Spring));
            }
        }
        else
        {
            Out.ParseWarnings.Add(TEXT("No 'boneGroups' array found in VRM 0.x secondaryAnimation data"));
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

        UE_LOG(LogVRMSpring, Verbose, TEXT("Starting VRM spring bone parsing from JSON (%d characters)"), Json.Len());

        if (Json.IsEmpty())
        {
            OutError = TEXT("Empty JSON.");
            UE_LOG(LogVRMSpring, Warning, TEXT("VRM Spring Bone Parse Failed: %s"), *OutError);
            return false;
        }

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            OutError = TEXT("Failed to parse JSON.");
            UE_LOG(LogVRMSpring, Warning, TEXT("VRM Spring Bone Parse Failed: %s"), *OutError);
            return false;
        }

        // Try VRM1 first
        UE_LOG(LogVRMSpring, Verbose, TEXT("Attempting VRM 1.0 spring bone parsing..."));
        FString VRM1Error;
        if (ParseVRM1(Root, OutConfig, VRM1Error))
        {
            OutConfig.RawJson = Json;
            OutConfig.ParsedVersion = TEXT("VRM1.0");
            UE_LOG(LogVRMSpring, Log, TEXT("Successfully parsed VRM 1.0 spring bones: %s"), *OutConfig.GetParsingSummary());
            if (OutConfig.HasParseWarnings())
            {
                for (const FString& Warning : OutConfig.ParseWarnings)
                {
                    UE_LOG(LogVRMSpring, Warning, TEXT("VRM 1.0 Parse Warning: %s"), *Warning);
                }
                for (const FString& Unsupported : OutConfig.UnsupportedFeatures)
                {
                    UE_LOG(LogVRMSpring, Warning, TEXT("VRM 1.0 Unsupported Feature: %s"), *Unsupported);
                }
            }
            return true;
        }

        // Reset error, try VRM0
        UE_LOG(LogVRMSpring, Verbose, TEXT("VRM 1.0 failed (%s), attempting VRM 0.x parsing..."), *VRM1Error);
        FString Err0;
        FVRMSpringConfig As0;
        if (ParseVRM0(Root, As0, Err0))
        {
            OutConfig = MoveTemp(As0);
            OutConfig.RawJson = Json;
            OutConfig.ParsedVersion = TEXT("VRM0.x");
            OutError.Reset();
            UE_LOG(LogVRMSpring, Log, TEXT("Successfully parsed VRM 0.x spring bones: %s"), *OutConfig.GetParsingSummary());
            if (OutConfig.HasParseWarnings())
            {
                for (const FString& Warning : OutConfig.ParseWarnings)
                {
                    UE_LOG(LogVRMSpring, Warning, TEXT("VRM 0.x Parse Warning: %s"), *Warning);
                }
                for (const FString& Unsupported : OutConfig.UnsupportedFeatures)
                {
                    UE_LOG(LogVRMSpring, Warning, TEXT("VRM 0.x Unsupported Feature: %s"), *Unsupported);
                }
            }
            return true;
        }

        // No spring data found
        OutError = FString::Printf(TEXT("No VRM spring bone data detected. VRM1 error: %s, VRM0 error: %s"), *VRM1Error, *Err0);
        UE_LOG(LogVRMSpring, Warning, TEXT("VRM Spring Bone Parse Failed: %s"), *OutError);
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