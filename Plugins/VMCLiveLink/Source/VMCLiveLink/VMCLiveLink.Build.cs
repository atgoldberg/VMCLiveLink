using UnrealBuildTool;

public class VMCLiveLink : ModuleRules
{
    public VMCLiveLink(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "LiveLinkInterface",
            "LiveLink",
            "OSC",

            // JSON (FJsonValue, FJsonSerializer, FJsonObject etc.)
            "Json",
            "JsonUtilities",

            // DeveloperSettings (UDeveloperSettings / project settings)
            "DeveloperSettings",

            // If you need LiveLink animation helpers / remap types
            "LiveLinkAnimationCore"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            // used by AutoDetectAndApplyMapping()
            "AssetRegistry"
        });

        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new[]
            {
                // Editor-only functionality (creating assets, factories, editor UI)
                "UnrealEd",
                "AssetTools"
            });
        }
    }
}
