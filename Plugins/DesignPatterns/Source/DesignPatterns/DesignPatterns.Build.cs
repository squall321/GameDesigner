// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

public class DesignPatterns : ModuleRules
{
	public DesignPatterns(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"NetCore",            // FFastArraySerializer NetDeltaSerialize, FNetworkGUID
			"DeveloperSettings"   // UDP_DeveloperSettings
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",          // on-screen debug draw
			"Projects",           // IPluginManager
			"AssetRegistry",      // load-free tag index for the data registry
			"EngineSettings"
		});

		// FInstancedStruct / TInstancedStruct live in the StructUtils plugin module on
		// UE 5.3/5.4, but were merged into CoreUObject in 5.5. Version-gate to stay portable.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}

		// AIModule is deliberately NOT a dependency — the FSM blackboard is reached through
		// the IDP_BlackboardProvider seam so this plugin never forces an AIModule link.
	}
}
