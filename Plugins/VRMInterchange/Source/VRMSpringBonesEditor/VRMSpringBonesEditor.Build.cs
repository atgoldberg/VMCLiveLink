using UnrealBuildTool;

public class VRMSpringBonesEditor : ModuleRules
{
    public VRMSpringBonesEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        // NOTE: Do NOT throw when not building editor. For an UncookedOnly module,
        // UnrealBuildTool already excludes it from cooked targets. Throwing here
        // causes the module to be classified as strictly Editor-only, triggering
        // the blueprint compiler warning about editor-only nodes in runtime BPs.

        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "AnimGraph",
            "BlueprintGraph",
            "KismetCompiler",
            "GraphEditor",
            "Slate",
            "SlateCore",
            "ToolMenus",
            "VRMSpringBonesRuntime",
            "VRMInterchange"
        });

        PrivateIncludePaths.AddRange(new string[]
        {
            System.IO.Path.Combine(ModuleDirectory, "..", "VRMSpringBonesRuntime", "Public")
        });
    }
}
