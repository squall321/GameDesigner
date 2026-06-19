// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// OPT-IN module: this is the ONLY place the plugin links GameplayAbilities.
// To use it, the host project must (1) enable the GameplayAbilities plugin and
// (2) add a { "Name": "DesignPatternsGAS", "Type": "Runtime" } entry to the
// DesignPatterns.uplugin Modules array (or its own .uproject). It is intentionally
// left out of the default .uplugin so projects without GAS still compile.
public class DesignPatternsGAS : ModuleRules
{
	public DesignPatternsGAS(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"GameplayAbilities",   // SOLE GAS link in the whole plugin
			"GameplayTasks",
			"DesignPatterns"
		});
	}
}
