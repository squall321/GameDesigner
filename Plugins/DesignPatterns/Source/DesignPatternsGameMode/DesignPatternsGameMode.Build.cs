// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

public class DesignPatternsGameMode : ModuleRules
{
	public DesignPatternsGameMode(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags", "NetCore",
			"DesignPatterns", "DesignPatternsSeams",
			// Scoreboard ViewModel derives from UDP_ViewModelBase (DesignPatternsUI) on FieldNotification.
			"DesignPatternsUI", "FieldNotification",
			// Respawn resolves the ILvl_SpawnRegionProvider seam + its LvlTags service key from here.
			"DesignPatternsLevelDirector"
		});

		// The world-hub query seam (IWorldHub_Queryable) is consumed by the _HubFlag ruleset
		// condition to gate match outcomes on designer-authored world flags. Resolved at runtime
		// via the service locator; an absent hub degrades the condition to its documented default.
		PrivateDependencyModuleNames.AddRange(new string[] { "DesignPatternsWorld" });

		PrivateDependencyModuleNames.AddRange(new string[] { "DeveloperSettings" });

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
