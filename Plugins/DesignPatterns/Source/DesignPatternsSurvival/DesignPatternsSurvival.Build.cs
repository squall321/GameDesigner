// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

public class DesignPatternsSurvival : ModuleRules
{
	public DesignPatternsSurvival(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"NetCore",        // DOREPLIFETIME / replicated UPROPERTYs on components & subsystem-host actor
			"DesignPatterns"  // core: data registry, subsystem bases/accessors, message bus, native tags, logging
		});

		// NOTE ON INVENTORY DEPENDENCY (HARD RULE for this module):
		// The brief allows depending on DesignPatternsRPG for inventory OR shipping a lightweight
		// resource store. The DesignPatternsRPG module is NOT present on disk in this plugin, so a
		// hard dependency would not link. We therefore ship a SELF-CONTAINED lightweight resource
		// store (FSurv_ResourceStore, see Resource/USurv_ResourceStoreComponent.h) that the crafting
		// system consumes from. To switch to RPG inventory later: add "DesignPatternsRPG" below and
		// route USurv_CraftingComponent's consume/produce calls at the marked seam.

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings"
		});

		// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4, merged into CoreUObject in 5.5.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
