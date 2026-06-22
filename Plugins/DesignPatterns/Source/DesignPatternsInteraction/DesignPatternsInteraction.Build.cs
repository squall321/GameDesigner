// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// General interaction system: interactable seam, interactor component, focus targeting, data-driven
// verbs, optional interaction-as-command. Depends only on core + Seams.
public class DesignPatternsInteraction : ModuleRules
{
	public DesignPatternsInteraction(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags", "NetCore",
			"DesignPatterns", "DesignPatternsSeams",
			// Framework base/seam modules (NOT sibling genre/HL modules): UDP_ViewModelBase for the
			// context-menu ViewModel, and the IHUD_Trackable seam for the world-space prompt marker.
			"DesignPatternsUI", "DesignPatternsHUD"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			// FieldNotification backs the context-menu ViewModel's observable fields (matching the
			// shipped SaveSystemUI ViewModel). Listed explicitly though DesignPatternsUI re-exports it.
			"FieldNotification"
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
