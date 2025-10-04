using UnrealBuildTool;

public class VRMSpringBonesEditor : ModuleRules
{
    public VRMSpringBonesEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "BlueprintGraph",
            "AnimGraph",
            "AnimGraphRuntime",
            "VRMSpringBonesRuntime"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "UnrealEd",
            "Slate",
            "SlateCore"
        });
    }
}
