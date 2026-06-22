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
			"DesignPatterns", "DesignPatternsSeams",
			"PhysicsCore"    // EPhysicalSurface used in PUBLIC footstep headers (surface bank/component/library)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			"AudioMixer"     // submix/bus control for the mix model + submix-effect chain overrides
		});

		// UQuartzClockHandle (bar-synced music transitions) lives in Engine (already a public dep);
		// its use is #if-guarded for 5.3-5.5 parity and the clock handle is supplied softly by callers.
		// USoundEffectSubmixPreset / UAudioMixerBlueprintLibrary (reverb-zone submix effects) are
		// reachable via AudioMixer; soft refs in headers keep the public surface free of concrete
		// AudioExtensions types so no extra public dependency is required.

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
