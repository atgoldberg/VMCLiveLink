using UnrealBuildTool;
using System.Collections.Generic;
using System.IO;

public class VRMInterchange : ModuleRules
{
    public VRMInterchange(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Public: our public headers expose Interchange Import interfaces
        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            // Expose Interchange types in public headers
            "InterchangeCore",
            "InterchangeImport",
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
            "SkeletalMeshDescription",
            "AnimationCore",
            "ImageWrapper",
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