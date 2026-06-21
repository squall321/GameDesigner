// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Persist/Seam_SaveSlotManager.h" // FSeam_SaveSlotInfo + ISeam_SaveSlotManager (seam, read-only)
#include "SaveX_SlotViewModel.generated.h"

class ISeam_SaveSlotManager;
class UDP_ServiceLocatorSubsystem;

/**
 * One row of save-slot metadata, projected from FSeam_SaveSlotInfo into UI-friendly fields.
 *
 * This is a plain BlueprintType USTRUCT (NOT a UObject and NOT replicated) so a save/load widget can bind a
 * list entry directly to it. It carries pre-formatted text (timestamp + playtime) so the widget never has to
 * re-implement formatting policy, plus the raw values for projects that want their own formatting.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSAVESYSTEMUI_API FSaveX_SlotRow
{
	GENERATED_BODY()

	/** Stable file/slot name (the value to pass back into a load/delete request). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|UI")
	FString SlotName;

	/** Player-facing label (falls back to SlotName when the header carried no display name). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|UI")
	FText DisplayName;

	/** Raw last-write timestamp (UTC as stored in the header). FDateTime(0) when unknown. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|UI")
	FDateTime Timestamp = FDateTime(0);

	/** Pre-formatted, localizable last-write timestamp for direct binding (empty when unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|UI")
	FText TimestampText;

	/** Raw accumulated playtime in seconds. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|UI")
	float PlaytimeSeconds = 0.f;

	/** Pre-formatted, localizable playtime (e.g. "12h 03m") for direct binding. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|UI")
	FText PlaytimeText;

	/** True when the slot exists on disk (an empty row is possible for a blank grid cell). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|UI")
	bool bExists = false;

	/** True when this is the most-recent slot (so the widget can badge the "Continue" target). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Save|UI")
	bool bIsMostRecent = false;
};

/** Broadcast (game thread) after a Refresh repopulates the rows, so a view can rebuild its list once. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSaveX_OnSlotsRefreshed);

/**
 * ViewModel backing a save/load slot UI.
 *
 * RESPONSIBILITIES (view-model only — it owns no save machinery and writes nothing):
 *  - Reads slot metadata through the shared ISeam_SaveSlotManager seam, resolved by tag from the
 *    UDP_ServiceLocatorSubsystem. It NEVER hard-includes the concrete slot-manager class, so the UI module
 *    stays decoupled from the SaveSystem policy implementation and degrades to an empty, logged list when no
 *    backend is registered (the documented inert default).
 *  - Projects each FSeam_SaveSlotInfo into a UI-friendly FSaveX_SlotRow (pre-formatted timestamp/playtime,
 *    most-recent badge) and exposes the rows + a derived "has any slots" flag as observable FieldNotify
 *    properties so a view re-reads only what changed.
 *  - Refreshes ON DEMAND (Refresh()), never on a tick: the owner/mediator calls Refresh after a save/load/
 *    delete completes. A SetServiceKeyOverride hook lets an unusual project relocate the seam key.
 *
 * It holds the seam WEAKLY via a re-resolve each Refresh (it never caches a hard TScriptInterface across
 * frames), so a torn-down backend cannot dangle. As a UI ViewModel it has no replicated state and no
 * authority concerns: reads are pure metadata.
 */
UCLASS(BlueprintType, meta = (DisplayName = "DP Save Slot ViewModel"))
class DESIGNPATTERNSSAVESYSTEMUI_API USaveX_SlotViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	USaveX_SlotViewModel();

	/**
	 * Re-read every known slot through the seam and rebuild the observable rows. Safe to call repeatedly; it
	 * broadcasts the changed fields (and OnSlotsRefreshed) only when the resulting list/flags actually differ
	 * from the previous read, so a bound list does not rebuild needlessly.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|UI")
	void Refresh();

	/** Clear all rows (e.g. when the screen closes). Broadcasts changes if the list was non-empty. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|UI")
	void Clear();

	/**
	 * Override the service-locator key used to resolve the slot-manager seam. Defaults to the configured/
	 * conventional SaveSystem key; set this only if a project deliberately relocated the service. Passing an
	 * invalid tag restores the default resolution.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|UI")
	void SetServiceKeyOverride(FGameplayTag InServiceKey);

	// ---- Observable (FieldNotify) read accessors ----

	/** The projected slot rows (observable). A view binds its list to this. */
	UFUNCTION(BlueprintCallable, BlueprintPure, FieldNotify, Category = "DesignPatterns|Save|UI")
	const TArray<FSaveX_SlotRow>& GetSlots() const { return Slots; }

	/** True when at least one slot exists (observable; backs an "empty state" panel). */
	UFUNCTION(BlueprintCallable, BlueprintPure, FieldNotify, Category = "DesignPatterns|Save|UI")
	bool HasAnySlots() const { return bHasAnySlots; }

	/** Number of rows currently projected (observable convenience for headers/counters). */
	UFUNCTION(BlueprintCallable, BlueprintPure, FieldNotify, Category = "DesignPatterns|Save|UI")
	int32 GetSlotCount() const { return Slots.Num(); }

	/** The most-recent slot name a "Continue" button should load (observable; empty when none). */
	UFUNCTION(BlueprintCallable, BlueprintPure, FieldNotify, Category = "DesignPatterns|Save|UI")
	FString GetMostRecentSlotName() const { return MostRecentSlotName; }

	// ---- Plain read helpers (not FieldNotify) ----

	/** Look up a single projected row by slot name. Returns false if no such row is present. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|UI")
	bool FindSlotRow(const FString& SlotName, FSaveX_SlotRow& OutRow) const;

	/** Re-query the seam for whether a specific slot exists right now (does not require a Refresh). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|UI")
	bool DoesSlotExist(const FString& SlotName) const;

	/** Broadcast after Refresh repopulates the rows (so a view can rebuild its list entries once). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save|UI")
	FSaveX_OnSlotsRefreshed OnSlotsRefreshed;

	// FieldNotify ids generated for the FieldNotify-tagged getters above.
	UE_FIELD_NOTIFICATION_DECLARE_FIELD(Slots);
	UE_FIELD_NOTIFICATION_DECLARE_FIELD(HasAnySlots);
	UE_FIELD_NOTIFICATION_DECLARE_FIELD(SlotCount);
	UE_FIELD_NOTIFICATION_DECLARE_FIELD(MostRecentSlotName);
	UE_FIELD_NOTIFICATION_DECLARE_ENUM_START(4)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_VALUE(Slots)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_VALUE(HasAnySlots)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_VALUE(SlotCount)
		UE_FIELD_NOTIFICATION_DECLARE_ENUM_VALUE(MostRecentSlotName)
	UE_FIELD_NOTIFICATION_DECLARE_ENUM_END()

private:
	/**
	 * Resolve the slot-manager seam fresh from the service locator (never cached across frames). Returns an
	 * unset TScriptInterface when no locator/backend is registered — the inert default that yields an empty UI.
	 */
	TScriptInterface<ISeam_SaveSlotManager> ResolveSlotManager() const;

	/** Resolve the effective service-locator key: the override if valid, else settings/conventional key. */
	FGameplayTag ResolveServiceKey() const;

	/** Project a single seam info into a UI row, formatting timestamp/playtime and the most-recent badge. */
	static FSaveX_SlotRow ProjectRow(const FSeam_SaveSlotInfo& Info, const FString& MostRecent);

	/** Format a localizable "Xh Ym" (or "Ym Zs") playtime string from raw seconds. */
	static FText FormatPlaytime(float Seconds);

	/** Format a localizable last-write timestamp; returns empty text for FDateTime(0). */
	static FText FormatTimestamp(const FDateTime& Timestamp);

	/** The projected, observable rows. Plain value array (no UObject refs) — no GC concern. */
	UPROPERTY(Transient)
	TArray<FSaveX_SlotRow> Slots;

	/** Cached derived flag (mirrors Slots.Num() > 0) so HasAnySlots is a cheap observable read. */
	bool bHasAnySlots = false;

	/** Cached most-recent slot name from the seam (for the "Continue" badge/observable). */
	FString MostRecentSlotName;

	/** Optional project override for the seam service key; invalid => use settings/conventional default. */
	FGameplayTag ServiceKeyOverride;
};
