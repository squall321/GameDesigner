// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/IClassDescriptor.h"
#include "HUD_ObjectiveTrackerViewModel.generated.h"

/**
 * One pinned/tracked objective as the view consumes it: id, title, progress fraction, state, pinned flag,
 * and an optional world location. The view binds the Objectives field and re-reads this flat array.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_ObjectiveView
{
	GENERATED_BODY()

	/** Stable identity tag for this objective. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Objective")
	FGameplayTag ObjectiveId;

	/** Already-localized display title. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Objective")
	FText Title;

	/** Current progress value (raw). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Objective")
	float ProgressCurrent = 0.f;

	/** Target progress value (raw; <= 0 means binary objective). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Objective")
	float ProgressTarget = 0.f;

	/** Normalized [0,1] progress fraction (0 for binary objectives). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Objective")
	float ProgressFraction = 0.f;

	/** Coarse state tag (active/complete/failed) for styling. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Objective")
	FGameplayTag StateTag;

	/** True if the player explicitly pinned this objective (vs auto-tracked). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Objective")
	bool bPinned = false;

	/** True when WorldLocation is meaningful. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Objective")
	bool bHasWorldLocation = false;

	/** World location (valid only when bHasWorldLocation). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Objective")
	FVector WorldLocation = FVector::ZeroVector;
};

/**
 * ViewModel projecting the tracked/pinned objectives into a flat, view-ready array.
 *
 * Mirrors the shipped VM IClassDescriptor pattern (hand-rolled EField + GetFieldId + descriptor + private
 * BroadcastField on UDP_ViewModelBase). Pure projection — no quest/gameplay pointers.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Objective Tracker ViewModel"))
class DESIGNPATTERNSHUD_API UHUD_ObjectiveTrackerViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this ViewModel's observable fields. */
	enum class EField : int32
	{
		/** The tracked-objective array. */
		Objectives = 0,
		/** Convenience pinned count for empty-state binding. */
		PinnedCount,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this ViewModel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/** Replace the tracked-objective set (called by the subsystem); broadcasts on change. */
	void SetObjectives(const TArray<FHUD_ObjectiveView>& InObjectives);

	/** The tracked objectives (copied for BP safety). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|Objective")
	TArray<FHUD_ObjectiveView> GetObjectives() const { return Objectives; }

	/** The count of pinned objectives. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|Objective")
	int32 GetPinnedCount() const;

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Backing storage for the projected objectives (observable field EField::Objectives). */
	UPROPERTY(Transient)
	TArray<FHUD_ObjectiveView> Objectives;
};
