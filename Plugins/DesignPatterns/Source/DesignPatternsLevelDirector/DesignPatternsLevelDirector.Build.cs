// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Procedural placement + level streaming + spawn regions. Wraps engine streaming/World Partition; scatters
// actors via the core Factory/Pool with a seeded RNG (deterministic); gates content via ISeam_ActivationGate
// and reads the grid via ISeam_TileProviderRead (both in Seams — no World/SimGrid hard dep). Core + Seams only.
public class DesignPatternsLevelDirector : ModuleRules
{
	public DesignPatternsLevelDirector(ReadOnlyTargetRules Target) : base(Target)
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
