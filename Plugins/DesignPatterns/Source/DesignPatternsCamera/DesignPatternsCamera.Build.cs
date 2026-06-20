// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Composable camera framework: priority/blend camera-mode stack, modes-as-strategy, shake-by-tag,
// targeting/lock-on. Wraps APlayerCameraManager rather than fighting it. Depends only on core + Seams.
public class DesignPatternsCamera : ModuleRules
{
	public DesignPatternsCamera(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"DesignPatterns", "DesignPatternsSeams",
			// DeveloperSettings is public because UCam_DeveloperSettings (a public type) derives from
			// UDeveloperSettings, so any consumer including that header needs the module on its API line.
			"DeveloperSettings"
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
