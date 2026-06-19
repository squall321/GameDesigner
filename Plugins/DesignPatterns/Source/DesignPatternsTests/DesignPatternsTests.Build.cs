// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Test module. Runtime type so it loads in editor/automation contexts, but every test body is
// guarded by `#if WITH_AUTOMATION_TESTS` so nothing ships. Add/remove from the .uplugin freely.
public class DesignPatternsTests : ModuleRules
{
	public DesignPatternsTests(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"GameplayTags",
			"DesignPatterns"   // system under test
		});

		// FInstancedStruct (used to build message payloads in the bus test) is in StructUtils on 5.3/5.4.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PrivateDependencyModuleNames.Add("StructUtils");
		}
	}
}
