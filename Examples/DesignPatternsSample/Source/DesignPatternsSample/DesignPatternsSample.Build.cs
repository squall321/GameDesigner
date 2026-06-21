// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// The sample game module. It depends on a representative slice of the DesignPatterns plugin modules to
// demonstrate composition — adding more is just adding more module names here (the seams keep them
// decoupled). This is the only place a real game wires concrete modules together.
public class DesignPatternsSample : ModuleRules
{
	public DesignPatternsSample(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",

			// Core + high-level
			"DesignPatterns", "DesignPatternsSeams", "DesignPatternsWorld",

			// Wave-3 gameplay/flow used by the sample
			"DesignPatternsSkillTree", "DesignPatternsProgression", "DesignPatternsGameMode",
			"DesignPatternsGameFlow", "DesignPatternsSaveSystem", "DesignPatternsWorldSystems"
		});
	}
}
