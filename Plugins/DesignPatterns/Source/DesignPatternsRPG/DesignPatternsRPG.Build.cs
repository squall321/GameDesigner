// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

public class DesignPatternsRPG : ModuleRules
{
	public DesignPatternsRPG(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"NetCore",          // FFastArraySerializer NetDeltaSerialize + push-model replication
			"DesignPatterns",   // core: Data registry, Save system, subsystem bases, native tags, log
			"DesignPatternsSeams" // ISeam_ItemQuery / ISeam_Reputation / ISeam_EntityIdentity (quest gates, objectives)
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",

			// World is a PRIVATE dependency: the objective tracker / journal resolve the concrete
			// UWorldHub_StateHubSubsystem (stage-visit flags, branch hub-writes, lore-unlock flags) only in
			// .cpp via FDP_SubsystemStatics — no World type is exposed in any RPG public header. RPG does NOT
			// depend on DesignPatternsNarrative; quest branching uses RPG-local gates/effects, not UNarr_*.
			"DesignPatternsWorld"
		});

		// FInstancedStruct / TInstancedStruct live in the StructUtils plugin module on
		// UE 5.3/5.4, but were merged into CoreUObject in 5.5. Version-gate to stay portable.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
