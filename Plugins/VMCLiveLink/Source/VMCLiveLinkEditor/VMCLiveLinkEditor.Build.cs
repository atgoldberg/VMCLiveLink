using UnrealBuildTool;

public class VMCLiveLinkEditor : ModuleRules
{
    public VMCLiveLinkEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]{
            "Core","CoreUObject","Engine"
        });

        PrivateDependencyModuleNames.AddRange(new[]{
            "VMCLiveLink",          // runtime module
            "LiveLink","LiveLinkInterface","LiveLinkEditor",
            "Slate","SlateCore","UnrealEd","AssetTools"
        });
    }
}
