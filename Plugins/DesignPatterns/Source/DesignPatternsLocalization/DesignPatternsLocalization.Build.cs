// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Localization + accessibility, wrapping UE's FText/StringTable/culture system. Culture get/set, subtitle
// queue, accessibility options broadcast to ISeam_AccessibilityConsumer, optional ISeam_TextToSpeech.
// Player-local; persisted via the game user settings / save. Depends on core + Seams + UI (subtitle VM).
public class DesignPatternsLocalization : ModuleRules
{
	public DesignPatternsLocalization(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"UMG", "FieldNotification",
			"DesignPatterns", "DesignPatternsUI", "DesignPatternsSeams"
		});

		// FText / StringTable / culture (FInternationalization) live in Core/CoreUObject — no extra module.
		PrivateDependencyModuleNames.AddRange(new string[] { "DeveloperSettings" });

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
