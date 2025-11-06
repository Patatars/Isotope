using UnrealBuildTool;

public class Isotope2ServerTarget : TargetRules
{
    public Isotope2ServerTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Server;
        DefaultBuildSettings = BuildSettingsVersion.V5;
        ExtraModuleNames.Add("Isotope2");
    }
}
