using UnrealBuildTool;

public class GraceAnimationRuntime : ModuleRules
{
    public GraceAnimationRuntime(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "AnimGraphRuntime" });
        // The original sampler has distinct multiply/add operations, without FMA.
        FPSemantics = FPSemanticsMode.Precise;
    }
}
