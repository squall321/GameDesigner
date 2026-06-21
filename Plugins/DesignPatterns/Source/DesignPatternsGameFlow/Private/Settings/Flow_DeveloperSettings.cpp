// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Settings/Flow_DeveloperSettings.h"
#include "DesignPatternsGameFlowModule.h"

UFlow_DeveloperSettings::UFlow_DeveloperSettings()
{
	// Defensive, shipped defaults. A project overrides these in DefaultGame.ini / Project Settings.
	// We reference the module's native tags directly so the CDO is sensible even with no config rows.
	InitialPhase          = FlowTags::Phase_Boot;
	MenuInputModeTag      = FlowTags::InputMode_Menu;
	GameInputModeTag      = FlowTags::InputMode_Game;
	PauseInputModeTag     = FlowTags::InputMode_Pause;

	// The default UI layer phase screens are pushed onto. This is a shared UI-layer contract tag; we
	// request it by name (a project's UI mediator owns the actual layer). RequestGameplayTag avoids a
	// hard dependency on the UI module's tag registry.
	DefaultScreenLayerTag = FGameplayTag::RequestGameplayTag(FName("DP.UI.Layer.Menu"), /*ErrorIfNotFound*/ false);
}

const UFlow_DeveloperSettings* UFlow_DeveloperSettings::Get()
{
	return GetDefault<UFlow_DeveloperSettings>();
}
