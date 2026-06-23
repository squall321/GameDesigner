// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/IClassDescriptor.h"
#include "UObject/WeakInterfacePtr.h"
#include "Stats/Seam_StatusProvider.h"
#include "HUD_StatusBarViewModel.generated.h"

class UHUD_StatusBarStyleDataAsset;
class UTexture2D;

/**
 * One status/buff bar entry as the view consumes it: icon + tint (resolved from the style asset), stacks,
 * the remaining-duration ring fraction, and the seam-neutral source data. The view binds the Statuses field
 * and re-reads this flat array — it never touches the concrete stats module.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_StatusEntryView
{
	GENERATED_BODY()

	/** The status identity tag. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	FGameplayTag StatusTag;

	/** Coarse category tag (buff/debuff/...) for grouping/coloring. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	FGameplayTag CategoryTag;

	/** Soft icon resolved from the style asset (may be unloaded / null). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Icon tint resolved from the style asset. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	FLinearColor Tint = FLinearColor::White;

	/** Current stack count (rendered as a badge when > 1). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	int32 Stacks = 1;

	/** Seconds remaining (<= 0 for indefinite). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	float RemainingSeconds = 0.f;

	/** Normalized [0,1] remaining fraction for the duration ring (1 for indefinite). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	float RemainingFraction = 1.f;

	/** Optional scalar magnitude for a text readout (0 when unused). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Status")
	float Magnitude = 0.f;
};

/**
 * ViewModel projecting an actor's active statuses (read through ISeam_StatusProvider) into a flat,
 * view-ready buff-bar array.
 *
 * Mirrors UHUD_MinimapViewModel / UHUD_NotificationViewModel (hand-rolled EField + GetFieldId + descriptor
 * + private BroadcastField on UDP_ViewModelBase). It holds the status source as a TWeakInterfacePtr (pruned
 * on use) and a style asset for icon/tint resolution — NO concrete stats pointer, NO FSeam_StatMod. The
 * owning HUD widget supplies the source (typically off the local pawn) and calls Refresh per tick.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Status Bar ViewModel"))
class DESIGNPATTERNSHUD_API UHUD_StatusBarViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this ViewModel's observable fields. */
	enum class EField : int32
	{
		/** The projected status entries. */
		Statuses = 0,
		/** Convenience active count for empty-state binding. */
		ActiveCount,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this ViewModel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/** Set (or clear) the status source the VM reads. Held weakly; safe to pass a stale interface. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Status")
	void SetStatusSource(const TScriptInterface<ISeam_StatusProvider>& InSource);

	/** Replace the icon/tint style asset. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Status")
	void SetStyleAsset(UHUD_StatusBarStyleDataAsset* InStyle);

	/** Re-read the source, project FSeam_StatusEntry -> FHUD_StatusEntryView, broadcast if changed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Status")
	void Refresh();

	/** The projected status entries (copied for BP safety). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|Status")
	TArray<FHUD_StatusEntryView> GetStatuses() const { return Statuses; }

	/** The count of active statuses. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|Status")
	int32 GetActiveCount() const { return Statuses.Num(); }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Backing storage for the projected status entries (observable field EField::Statuses). */
	UPROPERTY(Transient)
	TArray<FHUD_StatusEntryView> Statuses;

	/** The style asset for icon/tint resolution (owned ref, GC-kept). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_StatusBarStyleDataAsset> Style = nullptr;

	/** The status source, held weakly so a destroyed provider simply yields an empty bar. */
	TWeakInterfacePtr<ISeam_StatusProvider> StatusSource;
};
