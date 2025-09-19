using UnrealBuildTool;

public class VMCLiveLinkEditor : ModuleRules
{
    public VMCLiveLinkEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        // Minimal public runtime dependencies for editor module consumers
        PublicDependencyModuleNames.AddRange(new[] {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[] {
            "VMCLiveLink",          // runtime module (contains the UCLASS/header)
			"LiveLink",
            "LiveLinkInterface",
            "LiveLinkEditor",

			// Slate / editor UI
			"Slate",
            "SlateCore",
            "EditorStyle",

			// Editor-only functionality used by the factory UI and asset tooling
			"UnrealEd",
            "AssetTools"
        });

        // Editor modules are only valid for editor builds; safe to rely on Target.bBuildEditor here,
        // but this module is already an Editor module so the build system will only compile it for editor targets.
    }
}