using UnrealBuildTool;
using System.Collections.Generic;
using System.IO;

public class VRMInterchange : ModuleRules
{
    public VRMInterchange(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            // If you expose Interchange types in public headers:
            // "InterchangeCore",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            // Interchange modules your translator uses:
            "InterchangeCore",
            "InterchangeImport",
            "InterchangeNodes",
            "InterchangeFactoryNodes",
            // Required for FMeshDescription and attribute APIs
            "MeshDescription",
            "StaticMeshDescription",
            "SkeletalMeshDescription", // FSkeletalMeshAttributes and skin weights
            "AnimationCore",           // UE::AnimationCore::FBoneWeights
            "ImageWrapper",            // for ImageWrapperModule.h (used by translator)
            "Json"                     // for FJsonSerializer/FJsonReader/etc.
            // If you start using FJsonObjectConverter:
            // "JsonUtilities"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new string[]
            {
                "Core",
                "CoreUObject",
                "Engine",
                "InterchangeCore",
                "InterchangeEngine",
                "InterchangeEditor",
                "InterchangeNodes",
                "InterchangeFactoryNodes",
                "MeshDescription",
                "StaticMeshDescription",
                "SkeletalMeshDescription",
                "AnimationCore",
                "ImageWrapper",
                "Slate",
                "SlateCore",
                "Projects",
                "Json", // ensure editor build also links Json

                // <- Editor-only modules required by VRMSpringBonesPostImportPipeline.cpp
                "AssetRegistry",
                "AssetTools",
                // Add "UnrealEd" only if needed (many editor APIs are in UnrealEd)
                // "UnrealEd",
            });
        }

        // Add ThirdParty include path for cgltf (put cgltf.h into Plugins/VRMInterchange/ThirdParty/cgltf/)
        string ThirdPartyCgltf = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "cgltf");
        if (Directory.Exists(ThirdPartyCgltf))
        {
            PrivateIncludePaths.Add(ThirdPartyCgltf);
        }
    }
}