using UnrealBuildTool;

public class GraceAnimationEditor : ModuleRules
{
    public GraceAnimationEditor(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "UnrealEd", "GraceAnimationRuntime" });
        PrivateDependencyModuleNames.AddRange(new string[] { "Json", "AssetRegistry", "AnimationDataController" });
    }
}
