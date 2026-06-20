// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// HUD framework + advanced UI + input context: data-driven HUD layout, notification/toast queue,
// minimap/markers, menu navigation stack, Enhanced Input context layering. Built on the UI/MVVM module
// and the Platform input device seam. No genre deps; gameplay-affecting intents route through bus/seams.
public class DesignPatternsHUD : ModuleRules
{
	public DesignPatternsHUD(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"UMG", "Slate", "SlateCore", "FieldNotification", "EnhancedInput",
			"DesignPatterns", "DesignPatternsUI", "DesignPatternsSeams"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore", "DeveloperSettings",
			"DesignPatternsPlatform"   // input device seam
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
