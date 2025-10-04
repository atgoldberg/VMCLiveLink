#if WITH_DEV_AUTOMATION_TESTS

#if !__has_include("cgltf.h")
// cgltf is not available in this environment; compile a stub test.
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMSpringBonesNameResolveTests_Skipped, "VRM.SpringBones.NameResolve.SkippedNoCgltf", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMSpringBonesNameResolveTests_Skipped::RunTest(const FString& Parameters)
{
    // Skipped due to missing cgltf.h
    return true;
}

#else

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "VRMSpringBonesTypes.h"

// Local cgltf include (editor module has ThirdParty include path)
#include "cgltf.h"

static bool LocalResolveNamesFromGltf(const FString& Filename, FVRMSpringConfig& InOut)
{
    FTCHARToUTF8 PathUtf8(*Filename);
    cgltf_options Options = {};
    cgltf_data* Data = nullptr;
    const cgltf_result Res = cgltf_parse_file(&Options, PathUtf8.Get(), &Data);
    if (Res != cgltf_result_success || !Data) return false;
    struct FScoped { cgltf_data* D; ~FScoped(){ if (D) cgltf_free(D); } } Scoped{ Data };

    const int32 NodesCount = static_cast<int32>(Data->nodes_count);
    auto GetNodeName = [&](int32 NodeIndex)->FName
    {
        if (NodeIndex < 0 || NodeIndex >= NodesCount) return NAME_None;
        const cgltf_node* N = &Data->nodes[NodeIndex];
        return (N && N->name && N->name[0] != '\0') ? FName(UTF8_TO_TCHAR(N->name)) : NAME_None;
    };

    for (auto& C : InOut.Colliders)
        if (C.BoneName.IsNone() && C.NodeIndex != INDEX_NONE)
            C.BoneName = GetNodeName(C.NodeIndex);

    for (auto& J : InOut.Joints)
        if (J.BoneName.IsNone() && J.NodeIndex != INDEX_NONE)
            J.BoneName = GetNodeName(J.NodeIndex);

    for (auto& S : InOut.Springs)
        if (S.CenterBoneName.IsNone() && S.CenterNodeIndex != INDEX_NONE)
            S.CenterBoneName = GetNodeName(S.CenterNodeIndex);

    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMResolveNamesFromGltf, "VRM.SpringBones.ResolveNamesFromGltf",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMResolveNamesFromGltf::RunTest(const FString& Parameters)
{
    // Create a minimal .gltf on disk with 3 named nodes
    const FString TempDir = FPaths::ProjectSavedDir() / TEXT("VRMTests");
    IFileManager::Get().MakeDirectory(*TempDir, true);
    const FString GltfPath = TempDir / TEXT("NodesOnly.gltf");
    const FString GltfJson = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "nodes": [
        { "name":"Center" },
        { "name":"Head" },
        { "name":"Hair_01" }
      ]
    })JSON");
    if (!FFileHelper::SaveStringToFile(GltfJson, *GltfPath))
    {
        AddError(TEXT("Failed to write temporary .gltf"));
        return false;
    }

    // Build a config that references those node indices
    FVRMSpringConfig Cfg;
    Cfg.Spec = EVRMSpringSpec::VRM1;

    FVRMSpringCollider Collider;
    Collider.NodeIndex = 1; // Head
    Cfg.Colliders.Add(Collider);

    FVRMSpringJoint Joint;
    Joint.NodeIndex = 2; // Hair_01
    Cfg.Joints.Add(Joint);

    FVRMSpring Spring;
    Spring.CenterNodeIndex = 0; // Center
    Spring.JointIndices = { 0 }; // references Cfg.Joints[0]
    Cfg.Springs.Add(Spring);

    const bool bResolved = LocalResolveNamesFromGltf(GltfPath, Cfg);
    TestTrue(TEXT("ResolveNames succeeded"), bResolved);
    TestEqual(TEXT("Collider BoneName == Head"), Cfg.Colliders[0].BoneName, FName(TEXT("Head")));
    TestEqual(TEXT("Joint BoneName == Hair_01"), Cfg.Joints[0].BoneName, FName(TEXT("Hair_01")));
    TestEqual(TEXT("Spring CenterBoneName == Center"), Cfg.Springs[0].CenterBoneName, FName(TEXT("Center")));

    return true;
}

#endif // !__has_include("cgltf.h")

#endif // WITH_DEV_AUTOMATION_TESTS