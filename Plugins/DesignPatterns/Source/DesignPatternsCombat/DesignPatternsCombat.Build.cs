// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Action/Combat genre module built ON TOP of the core "DesignPatterns" module.
// Provides networked health, hit detection, combos and status effects, reusing the
// core message bus / subsystem accessors. To use it the host project must add a
// { "Name": "DesignPatternsCombat", "Type": "Runtime" } entry to the
// DesignPatterns.uplugin Modules array (or its own .uproject).
public class DesignPatternsCombat : ModuleRules
{
	public DesignPatternsCombat(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"NetCore",          // push-model / replication + FFastArraySerializer for the status-stack array
			"DesignPatterns",   // core: subsystem bases, message bus, accessors, native tags, action component
			"DesignPatternsSeams" // shared cross-module seams: StatModifierSink, DamageReactor, HitRewindTarget,
			                      // NeedProvider, TeamAffinity, EntityIdentity (TScriptInterface in public headers)
		});
	}
}
