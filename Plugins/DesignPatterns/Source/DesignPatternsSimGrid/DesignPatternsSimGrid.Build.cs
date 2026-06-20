// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Simulation grid/tile world + placement. Implements the shared read seam so consumers (agents,
// economy) read the grid without depending on this module. Depends only on core + Seams.
public class DesignPatternsSimGrid : ModuleRules
{
	public DesignPatternsSimGrid(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags", "NetCore",
			"DesignPatterns", "DesignPatternsSeams"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "DeveloperSettings" });

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
