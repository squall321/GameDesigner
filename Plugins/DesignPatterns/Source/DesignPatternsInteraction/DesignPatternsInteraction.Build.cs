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
			"DesignPatterns", "DesignPatternsSeams"
		});

		PrivateDependencyModuleNames.AddRange(new string[] { "DeveloperSettings" });

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
