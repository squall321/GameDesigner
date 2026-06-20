// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "HUD_DeveloperSettings.generated.h"

class UHUD_InputActionMapDataAsset;

/**
 * Project settings for the DesignPatternsHUD module (Project Settings > Plugins > DesignPatterns HUD).
 *
 * Centralizes the few project-level choices the HUD subsystems read at startup: the default input action
 * map, which input layers are added on initialize, and the default menu input-mode wiring. Everything is an
 * EditAnywhere config property (no magic constants in code); the subsystems read this CDO and fall back to
 * documented defensive defaults when it is unavailable (e.g. very early load).
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "DesignPatterns HUD"))
class DESIGNPATTERNSHUD_API UHUD_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHUD_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Groups these settings under the Plugins category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	//~ End UDeveloperSettings

	/**
	 * The default HUD input action map applied by UHUD_InputContextSubsystem on initialize. A soft ref so
	 * the settings object stays lightweight; the subsystem loads it on first use. May be unset (the
	 * subsystem then starts with no layers and a project pushes one explicitly).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Input",
		meta = (AllowedClasses = "/Script/DesignPatternsHUD.HUD_InputActionMapDataAsset"))
	TSoftObjectPtr<UHUD_InputActionMapDataAsset> DefaultInputActionMap;

	/**
	 * Layer tags added automatically when the input context subsystem initializes (typically the
	 * always-on gameplay layer). Each must be a layer the DefaultInputActionMap defines.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Input", meta = (Categories = "DP.HUD.InputLayer"))
	FGameplayTagContainer DefaultActiveLayers;

	/**
	 * Input-mode tag the menu stack pushes through the shared ISeam_InputModeArbiter while any menu screen
	 * is open. Defaults (in the constructor) to DP.Input.Mode.Menu; a project can repoint it.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Menu", meta = (Categories = "DP.Input.Mode"))
	FGameplayTag MenuInputModeTag;

	/**
	 * Priority the menu input mode is pushed at on the arbiter. Higher beats lower competing pushes (a
	 * cutscene lock, photo mode). Tunable so projects can order their UI/game input owners.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Menu", meta = (ClampMin = "0"))
	int32 MenuInputModePriority = 100;

	/**
	 * The default UI layer tag menu screens are pushed onto via the core UI mediator (e.g. DP.UI.Layer.Menu).
	 * Used by the menu stack when a push request does not specify a layer.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Menu", meta = (Categories = "DP.UI.Layer"))
	FGameplayTag DefaultMenuLayerTag;

	/** Convenience CDO accessor (never null at runtime for a UDeveloperSettings). */
	static const UHUD_DeveloperSettings* Get();
};
