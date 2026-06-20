// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// World data hub: the central query/mutation spine for game-wide state (flags, variables, counters,
// scoped blackboards) with save and optional replication. Depends only on core + Seams.
public class DesignPatternsWorld : ModuleRules
{
	public DesignPatternsWorld(ReadOnlyTargetRules Target) : base(Target)
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
