// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Dialogue + branching story + cutscene control. Story/quest state reads & gates through the World hub
// (IWorldHub_Queryable); dialogue presentation is decoupled via a presenter seam. LevelSequence is an
// optional private dep (the sequence-director area degrades gracefully when absent). No genre deps.
public class DesignPatternsNarrative : ModuleRules
{
	public DesignPatternsNarrative(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags", "NetCore",
			"DesignPatterns", "DesignPatternsSeams"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			"LevelSequence", "MovieScene",  // sequence/cutscene director (optional)

			// World is a PRIVATE dependency: the dialogue runner resolves the read-only
			// IWorldHub_Queryable seam from the service locator and effects resolve the concrete
			// hub via FDP_SubsystemStatics, all in .cpp — no World type is exposed in a public header.
			"DesignPatternsWorld"
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
