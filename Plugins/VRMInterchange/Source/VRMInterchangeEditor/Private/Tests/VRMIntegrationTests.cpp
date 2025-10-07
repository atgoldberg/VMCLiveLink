// Copyright (c) 2025 Lifelike & Believable Animation Design, Inc. | Athomas Goldberg. All Rights Reserved.
// Copyright (c) 2024 Lifelike & Believable Animation Design, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "VRMSpringBonesParser.h"
#include "VRMSpringBonesTypes.h"
#include "VRMInterchangeSettings.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMIntegrationTestVRM10, "VRM.Integration.ParseVRM10File",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMIntegrationTestVRM10::RunTest(const FString& Parameters)
{
    // Create minimal VRM 1.0 test file
    const FString TestJson = TEXT(R"JSON({
  "asset": {
    "generator": "VRM Test Generator",
    "version": "2.0"
  },
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [
    {"name": "Root"},
    {"name": "Head", "translation": [0, 1.6, 0]},
    {"name": "Hair_01", "translation": [0, 1.8, 0]},
    {"name": "Hair_02", "translation": [0, 1.9, 0]}
  ],
  "extensions": {
    "VRMC_springBone": {
      "specVersion": "1.0",
      "colliders": [
        {
          "node": 1,
          "shapes": [
            {
              "sphere": {
                "offset": [0, 0, 0],
                "radius": 0.15
              }
            }
          ]
        }
      ],
      "colliderGroups": [
        {
          "name": "HeadCollider",
          "colliders": [0]
        }
      ],
      "joints": [
        {
          "node": 2,
          "hitRadius": 0.02
        },
        {
          "node": 3,
          "hitRadius": 0.015
        }
      ],
      "springs": [
        {
          "name": "HairSpring",
          "joints": [0, 1],
          "colliderGroups": [0],
          "center": 0,
          "stiffness": 0.8,
          "drag": 0.2,
          "gravityDir": [0, -1, 0],
          "gravityPower": 0.1,
          "hitRadius": 0.02
        }
      ]
    }
  }
})JSON");

    // Test parsing
    FVRMSpringConfig Config;
    FString Error;
    
    const bool bSuccess = VRM::ParseSpringBonesFromJson(TestJson, Config, Error);
    
    TestTrue(TEXT("VRM 1.0 parsing succeeds"), bSuccess);
    
    if (bSuccess)
    {
        TestEqual(TEXT("VRM 1.0 spec detected"), Config.Spec, EVRMSpringSpec::VRM1);
        TestEqual(TEXT("Colliders count"), Config.Colliders.Num(), 1);
        TestEqual(TEXT("Collider groups count"), Config.ColliderGroups.Num(), 1);
        TestEqual(TEXT("Joints count"), Config.Joints.Num(), 2);
        TestEqual(TEXT("Springs count"), Config.Springs.Num(), 1);
        
        // Validate spring configuration
        if (Config.Springs.Num() > 0)
        {
            const FVRMSpring& Spring = Config.Springs[0];
            TestEqual(TEXT("Spring name"), Spring.Name, FString(TEXT("HairSpring")));
            TestEqual(TEXT("Spring joint count"), Spring.JointIndices.Num(), 2);
            TestEqual(TEXT("Spring collider group count"), Spring.ColliderGroupIndices.Num(), 1);
            TestEqual(TEXT("Spring stiffness"), Spring.Stiffness, 0.8f);
            TestEqual(TEXT("Spring drag"), Spring.Drag, 0.2f);
        }
        
        // Validate collider configuration
        if (Config.Colliders.Num() > 0)
        {
            const FVRMSpringCollider& Collider = Config.Colliders[0];
            TestEqual(TEXT("Collider node index"), Collider.NodeIndex, 1);
            TestEqual(TEXT("Collider sphere count"), Collider.Spheres.Num(), 1);
            
            if (Collider.Spheres.Num() > 0)
            {
                TestEqual(TEXT("Sphere radius"), Collider.Spheres[0].Radius, 0.15f);
            }
        }
        
        TestTrue(TEXT("Config is valid"), Config.IsValid());
    }
    else
    {
        AddError(FString::Printf(TEXT("VRM 1.0 parsing failed: %s"), *Error));
    }
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMIntegrationTestVRM0x, "VRM.Integration.ParseVRM0xFile", 
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMIntegrationTestVRM0x::RunTest(const FString& Parameters)
{
    // Create minimal VRM 0.x test file
    const FString TestJson = TEXT(R"JSON({
  "asset": {
    "generator": "VRM Test Generator",
    "version": "2.0"
  },
  "scene": 0,
  "scenes": [{"nodes": [0]}],
  "nodes": [
    {"name": "Root"},
    {"name": "Head", "translation": [0, 1.6, 0]},
    {"name": "Hair_01", "translation": [0, 1.8, 0]},
    {"name": "Hair_02", "translation": [0, 1.9, 0]}
  ],
  "extensions": {
    "VRM": {
      "specVersion": "0.0",
      "secondaryAnimation": {
        "boneGroups": [
          {
            "comment": "Hair",
            "stiffness": 0.8,
            "gravityPower": 0.1,
            "gravityDir": [0, -1, 0],
            "dragForce": 0.2,
            "center": 0,
            "hitRadius": 0.02,
            "bones": [2, 3],
            "colliderGroups": [0]
          }
        ],
        "colliderGroups": [
          {
            "node": 1,
            "colliders": [
              {
                "offset": [0, 0, 0],
                "radius": 0.15
              }
            ]
          }
        ]
      }
    }
  }
})JSON");

    // Test parsing
    FVRMSpringConfig Config;
    FString Error;
    
    const bool bSuccess = VRM::ParseSpringBonesFromJson(TestJson, Config, Error);
    
    TestTrue(TEXT("VRM 0.x parsing succeeds"), bSuccess);
    
    if (bSuccess)
    {
        TestEqual(TEXT("VRM 0.x spec detected"), Config.Spec, EVRMSpringSpec::VRM0);
        TestEqual(TEXT("Springs count"), Config.Springs.Num(), 1);
        TestEqual(TEXT("Collider groups count"), Config.ColliderGroups.Num(), 1);
        
        // Validate spring configuration
        if (Config.Springs.Num() > 0)
        {
            const FVRMSpring& Spring = Config.Springs[0];
            TestEqual(TEXT("Spring stiffness"), Spring.Stiffness, 0.8f);
            TestEqual(TEXT("Spring drag"), Spring.Drag, 0.2f);
            TestEqual(TEXT("Spring gravity power"), Spring.GravityPower, 0.1f);
            TestEqual(TEXT("Spring hit radius"), Spring.HitRadius, 0.02f);
        }
        
        TestTrue(TEXT("Config is valid"), Config.IsValid());
    }
    else
    {
        AddError(FString::Printf(TEXT("VRM 0.x parsing failed: %s"), *Error));
    }
    
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVRMIntegrationTestPipelineSettings, "VRM.Integration.PipelineSettings",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVRMIntegrationTestPipelineSettings::RunTest(const FString& Parameters)
{
    // Test that VRM settings can be accessed and have reasonable defaults
    const UVRMInterchangeSettings* Settings = GetDefault<UVRMInterchangeSettings>();
    
    TestTrue(TEXT("Settings object exists"), Settings != nullptr);
    
    if (Settings)
    {
        // Test default values match expectations from design document
        TestTrue(TEXT("Generate spring bone data enabled by default"), Settings->bGenerateSpringBoneData);
        TestFalse(TEXT("Generate post-process ABP disabled by default"), Settings->bGeneratePostProcessAnimBP);
        TestFalse(TEXT("Assign post-process ABP disabled by default"), Settings->bAssignPostProcessABP);
        TestFalse(TEXT("Overwrite existing disabled by default"), Settings->bOverwriteExistingSpringAssets);
    }
    
    return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS