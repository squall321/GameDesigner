// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;
using System.IO;

public class DesignPatternsNet : ModuleRules
{
	public DesignPatternsNet(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"GameplayTags",
			"NetCore",          // FLifetimeProperty / push-model replication helpers
			"DesignPatterns"    // core: subsystem bases, log, native tag roots we build on
		});

		// Online session support is OPTIONAL. We only link the Online modules when they are
		// actually available in the build, and gate every call site behind WITH_DP_ONLINE so
		// the module compiles and runs (logging "online unavailable") when they are absent.
		// To detect availability without a hard dependency we probe the engine plugin folder
		// for the OnlineSubsystem module header.
		bool bOnlineAvailable = false;
		string EngineSourcePath = Path.Combine(Target.RelativeEnginePath, "Plugins", "Online", "OnlineSubsystem");
		string EngineSourcePathAlt = Path.Combine(Target.RelativeEnginePath, "Plugins", "Online", "OnlineBase");
		if (Directory.Exists(EngineSourcePath) || Directory.Exists(EngineSourcePathAlt))
		{
			bOnlineAvailable = true;
		}

		if (bOnlineAvailable)
		{
			PublicDependencyModuleNames.AddRange(new string[]
			{
				"OnlineSubsystem",
				"OnlineSubsystemUtils"
			});
			PublicDefinitions.Add("WITH_DP_ONLINE=1");
		}
		else
		{
			PublicDefinitions.Add("WITH_DP_ONLINE=0");
		}

		// FInstancedStruct lives in StructUtils on 5.3/5.4, CoreUObject on 5.5+. Mirror the core.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
