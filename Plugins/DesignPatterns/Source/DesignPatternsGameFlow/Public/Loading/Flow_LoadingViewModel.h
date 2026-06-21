// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Flow_LoadingViewModel.generated.h"

/**
 * ViewModel the loading screen UI binds to (built on the engine FieldNotification system via
 * UDP_ViewModelBase, NOT the optional MVVM plugin).
 *
 * The loading-screen subsystem owns this object and pushes progress/label/indeterminate flag into it;
 * the ViewModel raises field-changed notifications so any bound view re-reads. It holds NO gameplay
 * pointers and never reaches into the world — it is a pure projection of the loading state.
 *
 * Observable fields:
 *  - Progress        : normalized [0,1] progress (meaningful only when bIndeterminate is false).
 *  - StatusLabel     : the on-screen status text ("Loading Level", "Streaming Assets"...).
 *  - bIndeterminate  : true when the underlying load gives no fraction (drives a spinner vs a bar).
 *  - bVisible        : whether the loading UI should be shown at all.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Flow Loading ViewModel"))
class DESIGNPATTERNSGAMEFLOW_API UFlow_LoadingViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		Progress = 0,
		StatusLabel,
		bIndeterminate,
		bVisible,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/**
	 * Push the current loading state. Progress < 0 is treated as indeterminate (the ViewModel clamps the
	 * stored Progress to 0 and sets bIndeterminate). Broadcasts only the fields that actually changed.
	 */
	void SetLoadingState(float InProgress, const FText& InStatusLabel, bool bInVisible);

	/** Set just the visibility (e.g. hide on PostLoadMap); broadcasts on change. */
	void SetVisible(bool bInVisible);

	// --- Observable getters ---

	/** Normalized [0,1] progress (0 while indeterminate). */
	UFUNCTION(BlueprintPure, Category = "Flow|Loading")
	float GetProgress() const { return Progress; }

	/** Current status label. */
	UFUNCTION(BlueprintPure, Category = "Flow|Loading")
	FText GetStatusLabel() const { return StatusLabel; }

	/** True when the underlying load gives no fraction (show a spinner, not a bar). */
	UFUNCTION(BlueprintPure, Category = "Flow|Loading")
	bool IsIndeterminate() const { return bIndeterminate; }

	/** True when the loading UI should be visible. */
	UFUNCTION(BlueprintPure, Category = "Flow|Loading")
	bool IsVisible() const { return bVisible; }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Backing storage: normalized progress (0 while indeterminate). */
	UPROPERTY(Transient)
	float Progress = 0.f;

	/** Backing storage: status label. */
	UPROPERTY(Transient)
	FText StatusLabel;

	/** Backing storage: indeterminate flag. */
	UPROPERTY(Transient)
	bool bIndeterminate = true;

	/** Backing storage: visibility flag. */
	UPROPERTY(Transient)
	bool bVisible = false;
};
