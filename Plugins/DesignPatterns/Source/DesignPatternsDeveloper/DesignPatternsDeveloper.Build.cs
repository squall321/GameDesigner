// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Runtime module (NOT Type=Developer) so its console commands survive into Test builds —
// the command bodies are individually wrapped in `#if !UE_BUILD_SHIPPING` so nothing
// ships in a Shipping build.
public class DesignPatternsDeveloper : ModuleRules
{
	public DesignPatternsDeveloper(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayTags",
			"DesignPatterns",
			"DesignPatternsUI"
		});
	}
}
