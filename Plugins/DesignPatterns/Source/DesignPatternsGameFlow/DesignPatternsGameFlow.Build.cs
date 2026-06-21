// Copyright DesignPatterns plugin. All Rights Reserved.

using UnrealBuildTool;

// Top-level application/game flow: a tag-keyed flow FSM (Boot/Title/MainMenu/Lobby/Loading/InGame/
// Pause/Results) that drives level travel, pushes the per-phase screen, sets the input mode and
// remembers a "continue" target. Also owns the loading-screen wrapper around the engine MoviePlayer /
// PreLoadMap / PostLoadMap delegates, the loading progress ViewModel and the match-results ViewModel.
//
// Cross-module coupling is ONLY via shared seams (ISeam_AppFlowController is implemented here and
// registered; ISeam_InputModeArbiter / ISeam_SaveSlotManager / ISeam_ScoreSource are resolved through
// the service locator) and the message bus. This module hard-includes NO genre or sibling Wave-3
// concrete header. It wraps the engine's MoviePlayer/PreLoadMap machinery rather than reinventing it.
public class DesignPatternsGameFlow : ModuleRules
{
	public DesignPatternsGameFlow(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core", "CoreUObject", "Engine", "GameplayTags",
			"UMG", "Slate", "SlateCore", "FieldNotification",
			"DesignPatterns", "DesignPatternsUI", "DesignPatternsSeams"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"DeveloperSettings",
			"MoviePlayer"   // loading-screen wrapper (GetMoviePlayer / SetupLoadingScreen)
		});

		// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4; merged into CoreUObject in 5.5+.
		if (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion <= 4)
		{
			PublicDependencyModuleNames.Add("StructUtils");
		}
	}
}
