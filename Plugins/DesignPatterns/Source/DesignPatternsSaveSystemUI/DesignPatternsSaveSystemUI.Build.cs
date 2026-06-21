// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Thin UI companion to DesignPatternsSaveSystem: the save/load slot ViewModel only. Isolating the UI
// dependency here keeps the save core buildable with zero UI on a dedicated server.
public class DesignPatternsSaveSystemUI : ModuleRules
{
	public DesignPatternsSaveSystemUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"UMG", "FieldNotification",
			"DesignPatterns", "DesignPatternsUI", "DesignPatternsSeams", "DesignPatternsSaveSystem"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "DeveloperSettings" });

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
