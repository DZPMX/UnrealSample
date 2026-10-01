using UnrealBuildTool;

public class UnrealSampleEditor : ModuleRules
{
    public UnrealSampleEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealSample", "AnimGraph", "BlueprintGraph" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Sequencer", "MovieScene", "Slate", "SlateCore" });
    }
}
