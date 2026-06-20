// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Simulation economy: commodities, stockpiles, production/consumption chains, market price formation,
// trade. Drives off the shared sim-clock seam. Depends only on core + Seams (no Survival/RPG dep).
public class DesignPatternsSimEconomy : ModuleRules
{
	public DesignPatternsSimEconomy(ReadOnlyTargetRules Target) : base(Target)
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
