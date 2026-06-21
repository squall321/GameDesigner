// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/IClassDescriptor.h"
#include "GameplayTagContainer.h"
#include "Tutorial/Tut_TutorialTypes.h"

#include "Tut_TutorialViewModel.generated.h"

/**
 * ViewModel the tutorial UI binds to (built on the engine FieldNotification system via UDP_ViewModelBase,
 * NOT the optional MVVM plugin).
 *
 * UTut_TutorialSubsystem owns this object and pushes the current step into it; the ViewModel raises field
 * changes so any bound view re-reads. It holds NO gameplay pointers and never reaches into the world — it is
 * a pure projection of the active tutorial step.
 *
 * Observable fields:
 *  - bActive          : whether a tutorial is currently running (drives panel visibility).
 *  - Instruction      : the current step's instruction text.
 *  - HighlightTarget  : the UI target tag the current step highlights (for views that mirror it).
 *  - StepIndex        : zero-based active step index (INDEX_NONE when inactive).
 *  - StepCount        : total steps in the active tutorial (for "x / N" progress display).
 *  - TutorialTag      : the active tutorial's DataTag identity.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Tutorial ViewModel"))
class DESIGNPATTERNSTUTORIAL_API UTut_TutorialViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		bActive = 0,
		Instruction,
		HighlightTarget,
		StepIndex,
		StepCount,
		TutorialTag,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/**
	 * Push the full current-step state in one call (used by the subsystem on every step change). Updates and
	 * broadcasts only the fields that actually changed.
	 */
	void SetActiveStep(
		const FGameplayTag& InTutorialTag,
		int32 InStepIndex,
		int32 InStepCount,
		const FText& InInstruction,
		const FGameplayTag& InHighlightTarget);

	/** Clear all step state and mark inactive (used when a tutorial completes / is skipped / not running). */
	void ClearActive();

	// --- Observable getters ---

	/** True while a tutorial is running. */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	bool IsActive() const { return bActive; }

	/** The current step's instruction text. */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	FText GetInstruction() const { return Instruction; }

	/** The current step's UI highlight target tag (invalid when none). */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	FGameplayTag GetHighlightTarget() const { return HighlightTarget; }

	/** Zero-based active step index (INDEX_NONE when inactive). */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	int32 GetStepIndex() const { return StepIndex; }

	/** Total number of steps in the active tutorial. */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	int32 GetStepCount() const { return StepCount; }

	/** The active tutorial's DataTag identity (invalid when inactive). */
	UFUNCTION(BlueprintPure, Category = "Tutorial")
	FGameplayTag GetTutorialTag() const { return TutorialTag; }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Whether a tutorial is currently running. */
	UPROPERTY(Transient)
	bool bActive = false;

	/** The active step's instruction text. */
	UPROPERTY(Transient)
	FText Instruction;

	/** The active step's UI highlight target tag. */
	UPROPERTY(Transient)
	FGameplayTag HighlightTarget;

	/** Zero-based active step index (INDEX_NONE when inactive). */
	UPROPERTY(Transient)
	int32 StepIndex = INDEX_NONE;

	/** Total steps in the active tutorial. */
	UPROPERTY(Transient)
	int32 StepCount = 0;

	/** The active tutorial's DataTag identity. */
	UPROPERTY(Transient)
	FGameplayTag TutorialTag;
};
