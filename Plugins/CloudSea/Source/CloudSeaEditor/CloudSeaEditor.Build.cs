using UnrealBuildTool;

public class CloudSeaEditor : ModuleRules
{
	public CloudSeaEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"Engine",
				"CloudSea",
				"UnrealEd",
				"EditorFramework",
				"AssetRegistry",
				"ContentBrowser",
				"PropertyEditor",
				"InputCore",
				"Slate",
				"SlateCore",
				"RenderCore",
				"RHI",
			}
		);
	}
}
