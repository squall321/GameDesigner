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
			"DesignPatterns"    // core: Data registry, Save system, subsystem bases, native tags, log
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings"
		});

		// FInstancedStruct / TInstancedStruct live in the StructUtils plugin module on
		// UE 5.3/5.4, but were merged into CoreUObject in 5.5. Version-gate to stay portable.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
