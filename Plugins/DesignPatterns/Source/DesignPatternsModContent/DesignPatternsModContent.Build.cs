// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Mod / DLC / content-pack pipeline + runtime data validation, wrapping UE's plugin & AssetManager systems.
// Discovers/mounts content plugins, layers a tag->asset override registry over the core data registry, and
// validates packs at mount (fail-safe). Core + Seams only (Projects/PakFile/AssetRegistry are engine).
public class DesignPatternsModContent : ModuleRules
{
	public DesignPatternsModContent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"DesignPatterns", "DesignPatternsSeams"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			"Projects",       // IPluginManager mount/unmount
			"PakFile",        // runtime .pak mounting
			"AssetRegistry"   // discover assets in mounted packs
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}

		// Editor-only hot-reload watches the discovery directories via IDirectoryWatcher. Gating on
		// Target.bBuildEditor (not just WITH_EDITOR in the .cpp) is required so the module is not linked
		// into a shipping build — the entire hot-reload subsystem is compiled out there too.
		if (Target.bBuildEditor)
		{
			PrivateDependencyModuleNames.Add("DirectoryWatcher");
		}
	}
}
