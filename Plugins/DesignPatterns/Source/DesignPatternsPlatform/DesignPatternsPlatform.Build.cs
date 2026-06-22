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
			"DesignPatterns",     // core: subsystem bases, logging, native tag roots, service locator
			"DesignPatternsSeams" // the seams the Platform subsystems implement + self-register
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"ApplicationCore",    // FCoreDelegates application lifecycle (background/foreground/deactivate)
			"RHI",                // optional: GPU/feature-level signal for the performance tier
			"InputCore",          // force-feedback / device ids for haptics
			"Slate",              // FSlateApplication (display metrics / DPI)
			"SlateCore"           // FMargin / inset math in UPlat_DisplayLibrary + UPlat_DisplaySubsystem
		});
	}
}
