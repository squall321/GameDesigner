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
			// Slate/SlateCore for FSlateFontInfo composed inside ULoc_FontSubsystem ONLY. The font seam in
			// DesignPatternsSeams stays Slate-free (it returns soft font-face refs + a bool RTL); the
			// concrete FSlateFontInfo never crosses the seam, so the Seams leaf invariant is preserved.
			"Slate", "SlateCore",
			"DesignPatterns", "DesignPatternsUI", "DesignPatternsSeams"
		});

		// FText / StringTable / culture (FInternationalization) live in Core/CoreUObject — no extra module.
		// Engine covers USoundBase + FStreamableManager (asset loading) used by the voice subsystem.
		PrivateDependencyModuleNames.AddRange(new string[] { "DeveloperSettings" });

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
