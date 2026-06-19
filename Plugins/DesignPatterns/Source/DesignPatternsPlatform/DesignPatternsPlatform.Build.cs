// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

public class DesignPatternsPlatform : ModuleRules
{
	public DesignPatternsPlatform(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DesignPatterns"      // core: subsystem bases, logging, native tag roots
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",    // FCoreDelegates application lifecycle (background/foreground/deactivate)
			"RHI"                 // optional: GPU/feature-level signal for the performance tier
		});
	}
}
