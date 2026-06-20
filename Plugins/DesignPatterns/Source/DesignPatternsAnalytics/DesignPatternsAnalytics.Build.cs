// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Genre-agnostic analytics/telemetry: tag-keyed event recording, bus->event mapping, progression metrics,
// A/B experiments. Records through the shared ISeam_AnalyticsSink (host backs it with the engine provider);
// opt-in/consent-gated, PII-safe by the FSeam_NetValue attribute constraint. Depends only on core + Seams.
public class DesignPatternsAnalytics : ModuleRules
{
	public DesignPatternsAnalytics(ReadOnlyTargetRules Target) : base(Target)
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
			"Analytics"   // engine analytics framework (the host adapter forwards to IAnalyticsProvider)
		});

		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
