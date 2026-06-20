// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Genre-agnostic audio framework: tag-keyed sound manager/buses, adaptive layered music, mix snapshots,
// ambience. Reacts to gameplay through the message bus; depends only on core + Seams.
public class DesignPatternsAudio : ModuleRules
{
	public DesignPatternsAudio(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"DesignPatterns", "DesignPatternsSeams"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			"AudioMixer"   // submix/bus control for the mix model
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
