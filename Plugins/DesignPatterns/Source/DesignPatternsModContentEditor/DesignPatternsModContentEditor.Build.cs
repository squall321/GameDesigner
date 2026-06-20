// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Editor-only companion to DesignPatternsModContent: a data-validation commandlet hook for CI and editor
// validation of content packs (runs UDP_DataAsset::IsDataValid + custom rules over a pack's assets).
public class DesignPatternsModContentEditor : ModuleRules
{
	public DesignPatternsModContentEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"DesignPatterns", "DesignPatternsModContent"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd", "DataValidation", "AssetRegistry"
		});
	}
}
