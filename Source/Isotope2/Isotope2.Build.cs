using UnrealBuildTool;

public class Isotope2 : ModuleRules
{
    public Isotope2(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "InputCore",
            "HTTP",
            "Json",
            "JsonUtilities",
            "WebSockets",
            "CoreOnline",
            "EOSShared",
            "OnlineServicesEOSGS",
            "EOSSDK",
            "OnlineServicesInterface",
            "OnlineServicesCommon",
            "OnlineServicesEOS",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {

        });
    }
}
