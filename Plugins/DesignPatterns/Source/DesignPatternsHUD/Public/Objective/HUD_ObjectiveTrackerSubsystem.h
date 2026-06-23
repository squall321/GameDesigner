// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "MessageBus/DPMessage.h"
#include "Objective/Seam_ObjectiveSource.h"
#include "HUD_ObjectiveTrackerSubsystem.generated.h"

class UHUD_ObjectiveTrackerViewModel;
class UHUD_ObjectiveTrackable;
class UHUD_MarkerRegistrySubsystem;

/**
 * Local-player-scoped pinned/tracked-objective HUD controller.
 *
 * Reads the game's tracked objectives through ISeam_ObjectiveSource (resolved by Service_ObjectiveSource as
 * a TScriptInterface, held weakly) and/or refreshes on a quest bus channel. It maintains a local pin set,
 * projects FSeam_ObjectiveSnapshot -> FHUD_ObjectiveView into UHUD_ObjectiveTrackerViewModel, and — for each
 * tracked objective that carries a world location — keeps an OWNED UHUD_ObjectiveTrackable bridge registered
 * into the world UHUD_MarkerRegistrySubsystem so objectives appear on the minimap / world-indicator layers.
 *
 * The bridges are kept in an owning UPROPERTY array (the registry holds them weakly) and are unregistered +
 * dropped on Deinitialize. Purely local/cosmetic — never replicates, never mutates the quest system.
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_ObjectiveTrackerSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** The ViewModel the objective-tracker UI binds to (never null after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Objective")
	UHUD_ObjectiveTrackerViewModel* GetViewModel() const { return ViewModel; }

	/** Set (or clear) the objective source. Held weakly; safe to pass a stale interface. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Objective")
	void SetObjectiveSource(const TScriptInterface<ISeam_ObjectiveSource>& InSource);

	/** Set the marker kind tag used when registering objective world markers (default HUDTags::Marker_Objective). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Objective")
	void SetWorldMarkerTag(FGameplayTag InTag) { WorldMarkerTag = InTag; }

	/** Pin an objective by id (idempotent). Pins are shown first / styled as player-pinned. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Objective")
	void PinObjective(FGameplayTag ObjectiveId);

	/** Unpin a previously-pinned objective. Returns true if it was pinned. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Objective")
	bool UnpinObjective(FGameplayTag ObjectiveId);

	/** Re-read the source, reproject the VM, and reconcile world-marker bridges. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Objective")
	void Refresh();

private:
	/** Resolve the objective source from the service locator if none was explicitly set (held weakly). */
	void ResolveObjectiveSource();

	/** Quest/objective bus handler: refresh on any matching change. */
	void HandleObjectiveBus(const FDP_Message& Message);

	/** Periodic refresh (FTSTicker) so progress/location updates flow even without a bus signal. */
	bool TickTracker(float DeltaTime);

	/** Resolve the world marker registry (by service tag, else world subsystem); null in editor/preview. */
	UHUD_MarkerRegistrySubsystem* ResolveRegistry() const;

	/**
	 * Reconcile the owned trackable bridges against the current world-located objectives: update existing,
	 * create+register new, unregister+drop gone. Keeps the registry (weak) in sync with what we own.
	 */
	void ReconcileWorldMarkers(const TArray<FSeam_ObjectiveSnapshot>& Snapshots);

	/** The pure-projection ViewModel (owned, GC-kept). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_ObjectiveTrackerViewModel> ViewModel = nullptr;

	/**
	 * Owned trackable bridges keyed by objective id. OWNING refs (the registry holds them weakly), so this
	 * map keeps them alive; entries are unregistered + removed when an objective stops being world-located.
	 */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UHUD_ObjectiveTrackable>> WorldMarkers;

	/** The objective source, held weakly so a destroyed source simply empties the tracker. */
	TWeakInterfacePtr<ISeam_ObjectiveSource> ObjectiveSource;

	/** Player-pinned objective ids (a superset/override of the source's auto-tracked set). */
	TSet<FGameplayTag> PinnedIds;

	/** Marker kind tag used for objective world markers. */
	FGameplayTag WorldMarkerTag;

	/** FTSTicker handle driving TickTracker; removed in Deinitialize. */
	FTSTicker::FDelegateHandle TickerHandle;
};
