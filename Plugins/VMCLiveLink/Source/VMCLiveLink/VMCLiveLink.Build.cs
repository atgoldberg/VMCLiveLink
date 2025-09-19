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

        // Keep editor-only modules guarded so runtime packaging doesn't pull them in
        if (Target.bBuildEditor)
        {
            PrivateDependencyModuleNames.AddRange(new[]
            {
				// Editor-only functionality (creating assets, factories, editor UI)
				"UnrealEd",
                "AssetTools",

                // Slate for editor UI implemented in runtime module under WITH_EDITOR
                "Slate",
                "SlateCore"
            });
        }
    }
}