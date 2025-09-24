// Builds in Editor only
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "VRMSpringBonesParser.h"
#include "VRMSpringBonesTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVRM1Json, "VRM.SpringBones.Parse.VRM1",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVRM1Json::RunTest(const FString& Parameters)
{
    // Minimal VRM1 springBone JSON
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "extensions": {
        "VRMC_springBone": {
          "colliders": [
            { "node": 1, "shapes": [ { "sphere": { "offset":[0,0,0], "radius": 0.02 } } ] }
          ],
          "colliderGroups": [
            { "name": "HeadCG", "colliders": [ 0 ] }
          ],
          "joints": [
            { "node": 2, "hitRadius": 0.01 }
          ],
          "springs": [
            {
              "name":"Hair",
              "center": 0,
              "stiffness": 0.8,
              "drag": 0.2,
              "gravityDir": [0,0,-1],
              "gravityPower": 1.0,
              "hitRadius": 0.03,
              "joints": [0],
              "colliderGroups": [0]
            }
          ]
        }
      }
    })JSON");

    FVRMSpringConfig Cfg;
    FString Err;
    const bool bOk = VRM::ParseSpringBonesFromJson(Json, Cfg, Err);
    TestTrue(TEXT("Parse VRM1 JSON"), bOk);
    TestEqual(TEXT("Spec == VRM1"), (int32)Cfg.Spec, (int32)EVRMSpringSpec::VRM1);
    TestEqual(TEXT("Colliders"), Cfg.Colliders.Num(), 1);
    TestEqual(TEXT("ColliderGroups"), Cfg.ColliderGroups.Num(), 1);
    TestEqual(TEXT("Joints"), Cfg.Joints.Num(), 1);
    TestEqual(TEXT("Springs"), Cfg.Springs.Num(), 1);
    TestTrue(TEXT("IsValid()"), Cfg.IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVRM0Json, "VRM.SpringBones.Parse.VRM0",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVRM0Json::RunTest(const FString& Parameters)
{
    // Minimal VRM0 secondaryAnimation JSON
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "extensions": {
        "VRM": {
          "secondaryAnimation": {
            "colliderGroups": [
              { "node": 1, "colliders": [ { "offset":[0,0,0], "radius": 0.02 } ] }
            ],
            "boneGroups": [
              {
                "comment": "Hair",
                "center": 0,
                "stiffness": 0.7,
                "dragForce": 0.2,
                "gravityDir": [0,0,-1],
                "gravityPower": 1.0,
                "hitRadius": 0.03,
                "bones": [ 2 ],
                "colliderGroups": [ 0 ]
              }
            ]
          }
        }
      }
    })JSON");

    FVRMSpringConfig Cfg;
    FString Err;
    const bool bOk = VRM::ParseSpringBonesFromJson(Json, Cfg, Err);
    TestTrue(TEXT("Parse VRM0 JSON"), bOk);
    TestEqual(TEXT("Spec == VRM0"), (int32)Cfg.Spec, (int32)EVRMSpringSpec::VRM0);
    TestEqual(TEXT("Colliders"), Cfg.Colliders.Num(), 1);
    TestEqual(TEXT("ColliderGroups"), Cfg.ColliderGroups.Num(), 1);
    TestEqual(TEXT("Springs"), Cfg.Springs.Num(), 1);
    TestTrue(TEXT("Joints synthesized"), Cfg.Joints.Num() >= 1);
    TestTrue(TEXT("IsValid()"), Cfg.IsValid());
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS