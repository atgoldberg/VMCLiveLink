using UnrealBuildTool;

public class VRMSpringBonesRuntime : ModuleRules
{
    public VRMSpringBonesRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "AnimGraphRuntime",
            "AnimationCore"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "Engine",
            "CoreUObject"
        });
    }
}
