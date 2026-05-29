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
            "CoreOnline",
            "OnlineSubsystem",
            "OnlineSubsystemUtils",
            "OnlineSubsystemEOS",
            "EOSShared",
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "OnlineServicesEOSGS",
            "EOSSDK",
            "OnlineServicesInterface",
            "OnlineServicesCommon",
            "OnlineServicesEOS",
        });
    }
}