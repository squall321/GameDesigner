// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Entity-trait spine: gives any actor a stable identity and a composable set of traits/capabilities.
// Depends only on core + Seams, so no genre module is required and any genre composes by registering
// capabilities through the seams.
public class DesignPatternsEntity : ModuleRules
{
	public DesignPatternsEntity(ReadOnlyTargetRules Target) : base(Target)
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
