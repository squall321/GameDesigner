// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

public class DesignPatternsEditor : ModuleRules
{
	public DesignPatternsEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"DesignPatterns",
			"DesignPatternsUI"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"UnrealEd",
			"Slate",
			"SlateCore",
			"PropertyEditor",
			"ComponentVisualizers",
			"ToolMenus",
			"AssetTools",
			"EditorSubsystem",
			"GameplayTagsEditor"
		});

		// GameplayDebugger is only available outside Shipping/Test. Gate both the dependency
		// and the code with WITH_DP_GAMEPLAY_DEBUGGER so runtime modules never include its headers.
		if (Target.bBuildDeveloperTools ||
			(Target.Configuration != UnrealTargetConfiguration.Shipping &&
			 Target.Configuration != UnrealTargetConfiguration.Test))
		{
			PrivateDependencyModuleNames.Add("GameplayDebugger");
			PublicDefinitions.Add("WITH_DP_GAMEPLAY_DEBUGGER=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_DP_GAMEPLAY_DEBUGGER=0");
		}
	}
}
