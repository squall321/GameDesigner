// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Engine/World.h"
#include "Flow_FlowStateDefinition.generated.h"

/**
 * Per-phase definition for the top-level flow FSM. One asset describes one phase
 * (Boot/Title/MainMenu/Lobby/Loading/InGame/Pause/Results, or a game's own child phase):
 *  - the level/map to travel to when this phase is entered (optional — overlay phases like
 *    Pause/Results travel nowhere),
 *  - which phase tags this phase may transition TO (the allowed-transition edge set),
 *  - the screen tag to push onto the UI mediator while in this phase,
 *  - the input-mode tag to push through the shared ISeam_InputModeArbiter while in this phase.
 *
 * Identity is the inherited DataTag, which MUST be the phase tag (e.g. Flow.Phase.MainMenu) so the flow
 * subsystem can resolve a phase's definition by its phase tag through the data registry. All gameplay
 * tunables live here as designer data; the flow subsystem holds none of them inline.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Flow State Definition"))
class DESIGNPATTERNSGAMEFLOW_API UFlow_FlowStateDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The level/map this phase travels to on enter. Empty (null soft ref) means "stay in the current
	 * world" — used by overlay phases (Pause, Results) that change only the screen and input mode.
	 * The flow subsystem travels with OpenLevel / ClientTravel depending on net mode.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow|Level", meta = (AllowedTypes = "World"))
	TSoftObjectPtr<UWorld> Level;

	/**
	 * Optional gameplay options string appended to the travel URL (e.g. "?game=MyGameMode?listen").
	 * Designer-authored; never hard-coded in the subsystem.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow|Level")
	FString TravelOptions;

	/**
	 * When true the travel for this phase is "absolute" (full server travel / OpenLevel that resets the
	 * world). When false a seamless/client travel is preferred where the net mode allows it. Defensive
	 * default true (a hard map change is the safe behaviour for top-level flow).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow|Level")
	bool bAbsoluteTravel = true;

	/**
	 * The phases this phase may transition TO. If empty AND the project allows undeclared transitions
	 * (Flow settings bAllowUndeclaredTransitions), any transition is allowed; otherwise an empty set
	 * means this is a terminal phase. The flow subsystem consults this in CanEnterPhase.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow|Transitions", meta = (Categories = "Flow.Phase"))
	FGameplayTagContainer AllowedTransitions;

	/**
	 * The screen tag the flow pushes onto the UI mediator (via the DP.Bus.Flow.Screen.Push channel)
	 * while in this phase. Empty means this phase pushes no screen (e.g. Boot, or InGame where the HUD
	 * owns the viewport). Popped automatically when the phase is left.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow|UI", meta = (Categories = "DP.UI.Screen"))
	FGameplayTag ScreenTag;

	/**
	 * The UI layer the screen is pushed onto. Empty falls back to the project's default screen layer
	 * (Flow settings DefaultScreenLayerTag).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow|UI", meta = (Categories = "DP.UI.Layer"))
	FGameplayTag ScreenLayerTag;

	/**
	 * The input mode the flow pushes through the shared ISeam_InputModeArbiter while in this phase.
	 * Empty falls back to a phase-kind default chosen by the subsystem (menu/game/pause). Popped when
	 * the phase is left.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow|Input", meta = (Categories = "DP.Input.Mode"))
	FGameplayTag InputModeTag;

	/**
	 * When true, entering this phase pauses the game (UGameplayStatics::SetGamePaused) — used by the
	 * Pause phase. The flow subsystem unpauses on leave. Defensive default false.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Flow|Input")
	bool bPausesGame = false;

	/**
	 * True if this phase definition declares a transition to TargetPhase (or AllowedTransitions is empty
	 * and undeclared transitions are open). Pure helper for the flow subsystem.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Transitions")
	bool AllowsTransitionTo(FGameplayTag TargetPhase, bool bUndeclaredAllowed) const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Warns if DataTag is not under Flow.Phase (the asset's identity must be its phase tag). */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
