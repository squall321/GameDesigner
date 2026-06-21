// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Tutorial + contextual hint framework: a data-driven, message-bus-driven tutorial sequence runner and a
// priority-queued, cooldowned contextual hint system. Both are LOCAL/COSMETIC GameInstance subsystems that
// observe already-replicated gameplay through the core bus (DP.Bus.*) and read world-hub state through the
// IWorldHub_Queryable seam. The tutorial runner highlights UI through ISeam_UIHighlight, gates input through
// ISeam_InputModeArbiter, surfaces its current step through a UDP_ViewModelBase, and persists completed
// tutorials through ISeam_Persistable (wrapping the core save subsystem — no new serialization). No genre or
// sibling Wave-3 module is hard-linked; every cross-module hop is a seam or a bus tag.
public class DesignPatternsTutorial : ModuleRules
{
	public DesignPatternsTutorial(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"FieldNotification",
			"DesignPatterns",        // core: subsystem bases, bus, data registry, service locator, settings
			"DesignPatternsUI",      // UDP_ViewModelBase (FieldNotification ViewModel base)
			"DesignPatternsSeams"    // ISeam_Persistable / ISeam_UIHighlight / ISeam_InputModeArbiter / ISeam_AnalyticsSink
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			// World hub READ seam (IWorldHub_Queryable) used by the hub-flag/hub-counter conditions and the
			// hint trigger evaluation. This module depends ONLY on that read interface (resolved from the
			// service locator) and the shared flag value types — never on the hub's concrete subsystem.
			"DesignPatternsWorld"
		});

		// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4 and is merged into CoreUObject in 5.5+.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
