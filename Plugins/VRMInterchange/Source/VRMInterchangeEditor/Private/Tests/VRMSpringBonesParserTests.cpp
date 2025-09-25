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

// Additional edge case and validation tests
#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVRM1EdgeCases, "VRM.SpringBones.Parse.VRM1.EdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVRM1EdgeCases::RunTest(const FString& Parameters)
{
    // Test VRM 1.0 with missing and invalid fields
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "extensions": {
        "VRMC_springBone": {
          "specVersion": "1.0-beta",
          "colliders": [
            { "node": 1, "shapes": [ { "sphere": { "offset":[0,0,0], "radius": 0.02 } } ] },
            { "shapes": [ { "sphere": { "offset":[1,1,1] } } ] },
            { "node": -1, "shapes": [] }
          ],
          "colliderGroups": [
            { "name": "HeadCG", "colliders": [ 0, 5 ] },
            { "colliders": [ 0 ] }
          ],
          "joints": [
            { "node": 2, "hitRadius": 0.01 },
            { "hitRadius": -0.5 },
            {}
          ],
          "springs": [
            {
              "name":"Hair",
              "center": {"node": 0},
              "stiffness": 1.5,
              "drag": -0.2,
              "gravityDir": [0,0,0],
              "gravityPower": 1.0,
              "hitRadius": -0.03,
              "joints": [{"node": 3, "hitRadius": 0.01}, 10],
              "colliderGroups": [0, 5]
            },
            {
              "stiffness": 0.8,
              "joints": []
            }
          ]
        }
      }
    })JSON");

    FVRMSpringConfig Cfg;
    FString Err;
    const bool bOk = VRM::ParseSpringBonesFromJson(Json, Cfg, Err);
    TestTrue(TEXT("Parse VRM1 Edge Cases JSON"), bOk);
    TestEqual(TEXT("Spec == VRM1"), (int32)Cfg.Spec, (int32)EVRMSpringSpec::VRM1);
    TestTrue(TEXT("Has parse warnings"), Cfg.HasParseWarnings());
    TestTrue(TEXT("Warning count > 0"), Cfg.ParseWarnings.Num() > 5);
    TestEqual(TEXT("ParsedVersion"), Cfg.ParsedVersion, FString(TEXT("VRM1.0")));
    
    // Verify specific warnings exist
    bool bFoundRadiusWarning = false;
    bool bFoundIndexWarning = false;
    for (const FString& Warning : Cfg.ParseWarnings)
    {
        if (Warning.Contains(TEXT("missing 'radius'")))
        {
            bFoundRadiusWarning = true;
        }
        if (Warning.Contains(TEXT("invalid collider index")))
        {
            bFoundIndexWarning = true;
        }
    }
    TestTrue(TEXT("Found radius warning"), bFoundRadiusWarning);
    TestTrue(TEXT("Found index validation warning"), bFoundIndexWarning);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVRM0EdgeCases, "VRM.SpringBones.Parse.VRM0.EdgeCases",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVRM0EdgeCases::RunTest(const FString& Parameters)
{
    // Test VRM 0.x with missing fields and typos
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "extensions": {
        "VRM": {
          "specVersion": "0.0",
          "secondaryAnimation": {
            "colliderGroups": [
              { "node": 1, "colliders": [ { "offset":[0,0,0], "radius": 0.02 }, { "offset":[1,1,1] } ] },
              { "colliders": [] }
            ],
            "boneGroups": [
              {
                "comment": "Hair",
                "center": 0,
                "stiffiness": 1.5,
                "dragForce": -0.2,
                "gravityDir": [0,0,0],
                "gravityPower": 1.0,
                "hitRadius": -0.03,
                "bones": [ 2, -1 ],
                "colliderGroups": [ 0, 5 ]
              },
              {
                "bones": [],
                "colliderGroups": []
              }
            ]
          }
        }
      }
    })JSON");

    FVRMSpringConfig Cfg;
    FString Err;
    const bool bOk = VRM::ParseSpringBonesFromJson(Json, Cfg, Err);
    TestTrue(TEXT("Parse VRM0 Edge Cases JSON"), bOk);
    TestEqual(TEXT("Spec == VRM0"), (int32)Cfg.Spec, (int32)EVRMSpringSpec::VRM0);
    TestTrue(TEXT("Has parse warnings"), Cfg.HasParseWarnings());
    TestTrue(TEXT("Warning count > 0"), Cfg.ParseWarnings.Num() > 5);
    TestEqual(TEXT("ParsedVersion"), Cfg.ParsedVersion, FString(TEXT("VRM0.x")));

    // Verify specific warnings exist
    bool bFoundStiffinessTypo = false;
    bool bFoundRadiusWarning = false;
    for (const FString& Warning : Cfg.ParseWarnings)
    {
        if (Warning.Contains(TEXT("'stiffiness'")) && Warning.Contains(TEXT("typo")))
        {
            bFoundStiffinessTypo = true;
        }
        if (Warning.Contains(TEXT("missing 'radius'")))
        {
            bFoundRadiusWarning = true;
        }
    }
    TestTrue(TEXT("Found stiffiness typo warning"), bFoundStiffinessTypo);
    TestTrue(TEXT("Found radius warning"), bFoundRadiusWarning);
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseInvalidData, "VRM.SpringBones.Parse.InvalidData",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseInvalidData::RunTest(const FString& Parameters)
{
    FVRMSpringConfig Cfg;
    FString Err;
    
    // Test empty JSON
    bool bResult = VRM::ParseSpringBonesFromJson(TEXT(""), Cfg, Err);
    TestFalse(TEXT("Empty JSON fails"), bResult);
    TestTrue(TEXT("Error message set for empty JSON"), !Err.IsEmpty());
    
    // Test malformed JSON
    bResult = VRM::ParseSpringBonesFromJson(TEXT("{invalid json}"), Cfg, Err);
    TestFalse(TEXT("Malformed JSON fails"), bResult);
    TestTrue(TEXT("Error message set for malformed JSON"), !Err.IsEmpty());
    
    // Test JSON with no VRM extensions
    const FString NoVRMJson = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "extensions": {}
    })JSON");
    
    bResult = VRM::ParseSpringBonesFromJson(NoVRMJson, Cfg, Err);
    TestFalse(TEXT("No VRM extensions fails"), bResult);
    TestTrue(TEXT("Error mentions both VRM versions"), Err.Contains(TEXT("VRM1")) && Err.Contains(TEXT("VRM0")));
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseVersionPrecedence, "VRM.SpringBones.Parse.VersionPrecedence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseVersionPrecedence::RunTest(const FString& Parameters)
{
    // Test file with both VRM 0.x and 1.0 data - VRM 1.0 should take precedence
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "extensions": {
        "VRM": {
          "secondaryAnimation": {
            "boneGroups": [
              {
                "comment": "VRM0Hair",
                "center": 0,
                "stiffness": 0.5,
                "dragForce": 0.1,
                "bones": [ 1 ]
              }
            ]
          }
        },
        "VRMC_springBone": {
          "springs": [
            {
              "name":"VRM1Hair",
              "center": 2,
              "stiffness": 0.8,
              "joints": [{"node": 3, "hitRadius": 0.01}]
            }
          ]
        }
      }
    })JSON");

    FVRMSpringConfig Cfg;
    FString Err;
    const bool bOk = VRM::ParseSpringBonesFromJson(Json, Cfg, Err);
    TestTrue(TEXT("Parse dual version JSON"), bOk);
    TestEqual(TEXT("Spec == VRM1 (precedence)"), (int32)Cfg.Spec, (int32)EVRMSpringSpec::VRM1);
    TestEqual(TEXT("Spring name from VRM1"), Cfg.Springs[0].Name, FString(TEXT("VRM1Hair")));
    TestEqual(TEXT("ParsedVersion is VRM1.0"), Cfg.ParsedVersion, FString(TEXT("VRM1.0")));
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMParseLoggingValidation, "VRM.SpringBones.Parse.LoggingValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMParseLoggingValidation::RunTest(const FString& Parameters)
{
    // Test that parsing summary works correctly
    const FString Json = TEXT(R"JSON(
    {
      "asset": {"version":"2.0"},
      "extensions": {
        "VRMC_springBone": {
          "colliders": [
            { "node": 1, "shapes": [ { "sphere": { "offset":[0,0,0], "radius": 0.02 } } ] },
            { "node": 2, "shapes": [ { "sphere": { "offset":[1,1,1], "radius": 0.03 } } ] }
          ],
          "colliderGroups": [
            { "name": "Group1", "colliders": [ 0 ] },
            { "name": "Group2", "colliders": [ 1 ] }
          ],
          "joints": [
            { "node": 3, "hitRadius": 0.01 }
          ],
          "springs": [
            {
              "name":"Spring1",
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
    TestTrue(TEXT("Parse logging test JSON"), bOk);
    
    // Test parsing summary
    FString Summary = Cfg.GetParsingSummary();
    TestTrue(TEXT("Summary contains counts"), Summary.Contains(TEXT("2 colliders")) && Summary.Contains(TEXT("2 groups")) && Summary.Contains(TEXT("1 joints")) && Summary.Contains(TEXT("1 springs")));
    TestTrue(TEXT("Summary contains version"), Summary.Contains(TEXT("VRM1")));
    
    // Test validation methods
    TestTrue(TEXT("IsValid returns true"), Cfg.IsValid());
    
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS