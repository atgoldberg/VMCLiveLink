using UnrealBuildTool;

public class VMCLiveLink : ModuleRules
{
    public VMCLiveLink(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]{
            "LiveLink","LiveLinkInterface", "LiveLinkAnimationCore",
            "Core","CoreUObject","Engine", "DeveloperSettings"
        });

        PrivateDependencyModuleNames.AddRange(new[]{
          "OSC","Networking","Sockets",
          "Slate","SlateCore","Json",
          "JsonUtilities", 
        });
    }
}
