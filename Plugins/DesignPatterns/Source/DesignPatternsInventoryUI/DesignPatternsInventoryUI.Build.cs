// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Generalized inventory/grid WINDOW UI framework (bags, crafting grids, hotbars, equipment, shops,
// chests, skill grids). Binds to any backend through the IInvUI_ItemContainer seam, so it never
// depends on RPG/Survival/Combat. Built on the core UI/MVVM module.
public class DesignPatternsInventoryUI : ModuleRules
{
	public DesignPatternsInventoryUI(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"UMG", "Slate", "SlateCore", "FieldNotification", "InputCore",
			"DesignPatterns", "DesignPatternsUI", "DesignPatternsSeams"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "DeveloperSettings" });

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
