// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Combat_ComboComponent.generated.h"

class UCombat_ComboComponent;

/**
 * One node in the combo graph: an attack that may chain into a successor within an input window.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSCOMBAT_API FCombat_ComboStep
{
	GENERATED_BODY()

	/** Identity of this attack step (e.g. DP.Combat.Attack.Light1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Combo")
	FGameplayTag StepTag;

	/**
	 * Time, in seconds, after this step is entered during which a follow-up input chains to the
	 * next step. Pressing after the window lapses restarts the combo from the first step.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Combo", meta = (ClampMin = "0.0"))
	float ChainWindow = 0.6f;

	FCombat_ComboStep() = default;
};

/**
 * Fired when the combo advances to a new step.
 * @param Component the combo component.
 * @param StepTag   the tag of the step just entered.
 * @param StepIndex zero-based index of the step in the sequence.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FCombat_OnComboAdvanced,
	UCombat_ComboComponent*, Component, FGameplayTag, StepTag, int32, StepIndex);

/** Fired when the combo resets to idle (window lapsed or explicitly reset). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FCombat_OnComboReset, UCombat_ComboComponent*, Component);

/**
 * Input-window combo graph.
 *
 * DESIGN CHOICE: this uses a lightweight world-timer window rather than a core
 * UDP_StateMachineComponent. The combo here is a strictly linear chain whose only state is
 * "current step index + window deadline", so a full FSM definition asset would be overkill —
 * the timer approach keeps per-actor cost to two fields and needs no shared definition asset.
 * (For branching/conditional combo graphs, swap this for a UDP_StateMachineComponent whose
 * states are the steps and whose transitions are guarded by the input window; the core FSM is
 * built exactly for that. This component intentionally covers the common linear case.)
 *
 * Authority: combo bookkeeping is local-input driven and not replicated; it only sequences which
 * attack a player triggers. The attacks themselves go through the authority-guarded hitbox/health
 * components, so no server-authoritative state is mutated here.
 */
UCLASS(ClassGroup = (DesignPatternsCombat), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSCOMBAT_API UCombat_ComboComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UCombat_ComboComponent();

	//~ Begin UActorComponent
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	/**
	 * Feed one attack input. If pressed within the current step's chain window, advances to the
	 * next step; otherwise (or from idle) starts the combo at step 0.
	 * @return the tag of the step now active (invalid if the sequence is empty).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Combo")
	FGameplayTag PushInput();

	/** Reset the combo back to idle immediately. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsCombat|Combo")
	void ResetCombo();

	/** @return zero-based index of the current step, or INDEX_NONE when idle. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Combo")
	int32 GetCurrentStepIndex() const { return CurrentStepIndex; }

	/** @return tag of the current step, or an invalid tag when idle. */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Combo")
	FGameplayTag GetCurrentStepTag() const;

	/** @return true while a combo chain is in progress (not idle). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsCombat|Combo")
	bool IsComboActive() const { return CurrentStepIndex != INDEX_NONE; }

	/** Broadcast each time the combo advances to a step. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Combo")
	FCombat_OnComboAdvanced OnComboAdvanced;

	/** Broadcast when the combo resets to idle. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsCombat|Combo")
	FCombat_OnComboReset OnComboReset;

	/** Ordered list of combo steps. Index 0 is the opener; the last step ends the chain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Combo")
	TArray<FCombat_ComboStep> Steps;

	/**
	 * If true, an input after the LAST step's window wraps around to step 0 (looping combo);
	 * if false the combo resets and the next input opens a fresh chain.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsCombat|Combo")
	bool bLoopAtEnd = false;

protected:
	/** Index of the active step, or INDEX_NONE when idle. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Combo")
	int32 CurrentStepIndex = INDEX_NONE;

	/** World time (seconds) at which the current chain window closes. */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Combo")
	float WindowDeadline = 0.f;

private:
	/** Enter the step at the given index: set deadline, fire OnComboAdvanced. */
	void EnterStep(int32 Index);

	/** @return current world time in seconds, or 0 if no world. */
	float GetWorldTimeSeconds() const;
};
