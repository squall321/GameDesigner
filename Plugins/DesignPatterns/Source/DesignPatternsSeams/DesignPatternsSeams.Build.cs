// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Leaf "seams" module: shared cross-cutting interfaces + value types ONLY. Every other high-level
// module depends on this; this depends on nothing but the core. This is what makes "no two modules
// hard-depend on each other" literally true for the shared seams (clock, needs, persistable,
// entity-id, tile-read).
public class DesignPatternsSeams : ModuleRules
{
	public DesignPatternsSeams(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"NetCore",          // FSeam_NetValue::NetSerialize
			"DesignPatterns"    // core: subsystem conventions, logging, native-tag macros
		});

		// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
