// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Types/Interact_AvailabilityTypes.h"
#include "Interact_InputDriverComponent.generated.h"

class UInteract_InteractorComponent;
class UInteract_VerbDefinition;

/** Local delegate: the in-progress activation meter changed (alpha in [0,1]) for hold/charge UI. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FInteract_OnLocalProgress, FGameplayTag, Verb, float, Alpha);

/**
 * LOCAL input-to-intent translator placed beside UInteract_InteractorComponent on the player pawn.
 *
 * Translates raw press/release notifications (the project routes EnhancedInput into the
 * BlueprintCallable entry points, so this module never depends on EnhancedInput) into the right
 * interactor request for the focused verb's activation mode:
 *   - Tap        : interact on press.
 *   - Hold       : interact on press (the server measures the hold against the replicated start time);
 *                  releasing early sends RequestEndInteract(Cancelled). A LOCAL progress meter drives UI.
 *   - Charge     : accumulates a [0,1] charge while held; on release interacts and reports the charge.
 *   - DoubleTap  : a second press within the verb's window interacts.
 *   - Repeat     : while held, re-issues the interact every RepeatIntervalSeconds.
 *
 * AUTHORITY: the LOCAL meter is cosmetic only. Authoritative hold/charge completion is still measured
 * server-side from the replicated GetActiveStartServerTime (a client cannot shorten a hold). Local and
 * never replicated.
 */
UCLASS(ClassGroup = (DesignPatternsInteraction), meta = (BlueprintSpawnableComponent),
	HideCategories = (ComponentReplication, Cooking))
class DESIGNPATTERNSINTERACTION_API UInteract_InputDriverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInteract_InputDriverComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/** The project calls this when the interact input is pressed (e.g. from an EnhancedInput binding). */
	UFUNCTION(BlueprintCallable, Category = "Interact|Input")
	void NotifyInteractPressed();

	/** The project calls this when the interact input is released. */
	UFUNCTION(BlueprintCallable, Category = "Interact|Input")
	void NotifyInteractReleased();

	/** Current local charge alpha in [0,1] for a Charge verb (0 for non-charge / idle). For UI. */
	UFUNCTION(BlueprintPure, Category = "Interact|Input")
	float GetLocalChargeAlpha() const { return LocalChargeAlpha; }

	/** Current local activation alpha in [0,1] (hold progress or charge), for a unified meter widget. */
	UFUNCTION(BlueprintPure, Category = "Interact|Input")
	float GetLocalActivationAlpha() const;

	/** Fires each frame the activation meter advances while held (hold/charge). */
	UPROPERTY(BlueprintAssignable, Category = "Interact|Input")
	FInteract_OnLocalProgress OnLocalProgress;

protected:
	/**
	 * Optional verb to use when the focused interactable has no explicit desired verb. Empty = let the
	 * interactor resolve the interactable's default verb. A project may bind a specific verb here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Input", meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag DefaultVerbOverride;

private:
	/** Resolve the sibling interactor component on the owner. */
	UInteract_InteractorComponent* ResolveInteractor() const;

	/** Resolve the verb definition for the currently-focused interactable's effective verb, or null. */
	const UInteract_VerbDefinition* ResolveFocusVerbDef(FGameplayTag& OutVerb) const;

	/** Begin a fresh local gesture for the resolved focus verb. */
	void BeginGesture();

	/** Issue the interactor request for the active gesture's verb. */
	void FireInteract();

	/** The interactor this driver feeds. Non-owning. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInteract_InteractorComponent> Interactor;

	/** The verb the current gesture is bound to (resolved at press). */
	UPROPERTY(Transient)
	FGameplayTag ActiveVerb;

	/** Whether the input is currently held down. */
	bool bInputHeld = false;

	/** Local time (seconds) the current press began (world time). */
	double PressStartTime = 0.0;

	/** Local charge accumulator in [0,1] for Charge verbs. */
	float LocalChargeAlpha = 0.f;

	/** Local hold progress in [0,1] for Hold verbs (cosmetic; authority measures server-side). */
	float LocalHoldAlpha = 0.f;

	/** Whether the current gesture already fired (so a Hold/Charge does not double-fire). */
	bool bGestureFired = false;

	/** Accumulator (seconds) toward the next Repeat fire while held. */
	float RepeatAccumulator = 0.f;

	/** World time (seconds) of the previous tap, for DoubleTap detection. */
	double LastTapTime = -1.0;
};
