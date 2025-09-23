using UnrealBuildTool;
using System.IO;

public class VRMInterchangeEditor : ModuleRules
{
    public VRMInterchangeEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        // Editor-only
        if (!Target.bBuildEditor)
        {
            throw new BuildException("VRMInterchangeEditor is editor-only.");
        }

        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            // Interchange
            "InterchangeCore",
            "InterchangeEngine",
            "InterchangeEditor",
            "InterchangeNodes",
            "InterchangeFactoryNodes",
            "DeveloperSettings",
            // Asset authoring
            "AssetRegistry",
            "AssetTools",
            "UnrealEd",     // for package/asset creation utilities

            // We parse JSON directly from the VRM/GLB container
            "Json",

            "Projects",

            // Runtime plugin module this editor module depends on
            "VRMInterchange",
        });

        // We share some includes with the runtime module (optional)
        PrivateIncludePaths.AddRange(new string[]
        {
            Path.Combine(ModuleDirectory, "..", "VRMInterchange", "Public"),
            Path.Combine(ModuleDirectory, "Private")
        });

        // Add ThirdParty include path for cgltf (put cgltf.h into Plugins/VRMInterchange/ThirdParty/cgltf/)
        string ThirdPartyCgltf = Path.Combine(ModuleDirectory, "..", "..", "ThirdParty", "cgltf");
        if (Directory.Exists(ThirdPartyCgltf))
        {
            PrivateIncludePaths.Add(ThirdPartyCgltf);
        }
    }

}