// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

public class DesignPatternsSaveSystem : ModuleRules
{
	public DesignPatternsSaveSystem(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags", "NetCore",
			"DesignPatterns", "DesignPatternsSeams"
		});

		// ImageWrapper: PNG encode/decode for save-slot thumbnails. Kept PRIVATE so consumers do not inherit
		// it. FCompression / IFileManager / FScreenshotRequest already come from Engine + Core on the public
		// list, so no RHI/RenderCore is needed — the thumbnail capturer WRAPS the engine screenshot pipeline
		// rather than reading the back-buffer directly.
		PrivateDependencyModuleNames.AddRange(new string[] { "DeveloperSettings", "ImageWrapper" });

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
