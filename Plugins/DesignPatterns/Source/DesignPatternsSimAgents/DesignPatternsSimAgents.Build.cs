// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Simulation agents: sim clock (implements the shared clock seam), schedules, generalized needs,
// utility-AI brain (built on core Strategy), job board, and crowd steering. Depends on core + Seams +
// Entity. Survival/SimGrid/SimEconomy are reached only through seams (no hard dependency).
public class DesignPatternsSimAgents : ModuleRules
{
	public DesignPatternsSimAgents(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags", "NetCore",
			"DesignPatterns", "DesignPatternsSeams", "DesignPatternsEntity"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			"NavigationSystem",   // engine nav integration for steering (not a nav rewrite)
			"AIModule"            // MoveTo/path-following helpers used by the locomotion seam impl
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
