// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "Engine/StreamableManager.h"
#include "Blueprint/UserWidget.h"
#include "HUD_HudLayoutSubsystem.generated.h"

class UHUD_HudLayoutDataAsset;
class UDP_MessageBusSubsystem;
struct FHUD_LayoutSlot;
struct FDP_Message;

/**
 * One realised HUD slot: the live widget instance plus the slot definition that produced it and
 * its current visibility. Held as an owning UPROPERTY so the widget is GC-kept while the layout
 * owns it.
 */
USTRUCT()
struct DESIGNPATTERNSHUD_API FHUD_LiveSlot
{
	GENERATED_BODY()

	/** The slot identity tag (mirror of the layout slot's SlotTag) for fast lookup/debug. */
	UPROPERTY()
	FGameplayTag SlotTag;

	/** The layer this slot's widget was added to. */
	UPROPERTY()
	FGameplayTag LayerTag;

	/** ZOrder the widget was added with. */
	UPROPERTY()
	int32 ZOrder = 0;

	/** The realised widget instance (created via CreateWidget). Owning ref keeps it alive. */
	UPROPERTY()
	TObjectPtr<UUserWidget> Widget = nullptr;

	/** Current desired visibility of this slot. */
	UPROPERTY()
	bool bVisible = false;

	/** True while the soft widget class is still streaming and the widget has not been created. */
	UPROPERTY()
	bool bLoading = false;
};

/**
 * Local-player-scoped owner of the active HUD layout.
 *
 * Consumes a UHUD_HudLayoutDataAsset (resolved by tag through the core data registry, or set
 * directly) and creates/positions HUD widget slots on viewport layers for THIS local player.
 * Soft widget classes are streamed asynchronously via a shared FStreamableManager; the widget is
 * created and added to the viewport only once its class finishes loading, so the layout never
 * hard-loads every HUD widget up front.
 *
 * Show/Hide is by slot tag. Swapping the layout (ApplyLayout / ApplyLayoutByTag) tears down the
 * previous slots and rebuilds from the new asset. The HUD is purely LOCAL/COSMETIC — nothing here
 * replicates; it reacts to already-replicated gameplay surfaced on the message bus
 * (DP.Bus.HUD.LayoutRebuild / DP.Bus.HUD.SlotShow / DP.Bus.HUD.SlotHide).
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_HudLayoutSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Apply Layout for this local player: tear down any existing slots and (re)create the new ones.
	 * Slots flagged bVisibleByDefault are streamed + shown immediately; others are tracked hidden.
	 * Passing null clears the current layout.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Layout")
	void ApplyLayout(UHUD_HudLayoutDataAsset* Layout);

	/**
	 * Resolve LayoutTag through the core data registry to a UHUD_HudLayoutDataAsset and apply it.
	 * @return true if a layout was resolved and applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Layout")
	bool ApplyLayoutByTag(FGameplayTag LayoutTag);

	/** Tear down every slot and remove its widget from the viewport. The layout asset ref is kept. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Layout")
	void ClearLayout();

	/**
	 * Rebuild the current layout from scratch (tear down + re-apply the active asset). Used on a
	 * layout-asset hot-reload or a DP.Bus.HUD.LayoutRebuild broadcast.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Layout")
	void RebuildLayout();

	/**
	 * Show the slot identified by SlotTag, streaming + creating its widget on first show. No-op
	 * (with a warning) if the slot is not defined in the active layout.
	 * @return true if the slot exists in the active layout.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Layout")
	bool ShowSlot(FGameplayTag SlotTag);

	/**
	 * Hide the slot identified by SlotTag (collapses its widget; the instance is retained so a
	 * later ShowSlot is cheap). No-op if the slot is unknown.
	 * @return true if the slot exists in the active layout.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Layout")
	bool HideSlot(FGameplayTag SlotTag);

	/** True if SlotTag is defined in the active layout AND currently shown. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "HUD|Layout")
	bool IsSlotVisible(FGameplayTag SlotTag) const;

	/** Get the live widget instance for SlotTag, or null if not created/loaded yet. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "HUD|Layout")
	UUserWidget* GetSlotWidget(FGameplayTag SlotTag) const;

	/** The active layout asset, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "HUD|Layout")
	UHUD_HudLayoutDataAsset* GetActiveLayout() const { return ActiveLayout; }

	/** Append a multi-line dump of every live slot + its state to OutLines (for debug commands). */
	void DumpTo(TArray<FString>& OutLines) const;

private:
	/** Resolve the message bus (game-instance scoped) for THIS local player's world, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Bus handler: react to DP.Bus.HUD.LayoutRebuild / SlotShow / SlotHide on this local player. */
	void HandleHudBusMessage(const FDP_Message& Message);

	/** Begin (or reuse) the async stream for SlotTag's widget class, creating the widget when ready. */
	void StreamAndCreateSlotWidget(const FGameplayTag& SlotTag);

	/** Completion callback for a slot's class stream: create + add the widget if still wanted visible. */
	void OnSlotClassLoaded(FGameplayTag SlotTag);

	/** Create the widget for a now-loaded slot class, add it to the viewport layer, set visibility. */
	void CreateAndAddSlotWidget(const FHUD_LayoutSlot& SlotDef, FHUD_LiveSlot& LiveSlot, UClass* LoadedClass);

	/** Apply LiveSlot.bVisible to its widget's UMG visibility (Visible / Collapsed). */
	static void ApplyWidgetVisibility(FHUD_LiveSlot& LiveSlot);

	/** Remove LiveSlot's widget from the viewport/parent and cancel any in-flight stream. */
	void TeardownLiveSlot(FHUD_LiveSlot& LiveSlot);

	/** The owning local player's player controller (for CreateWidget owning-player), or null. */
	APlayerController* GetOwningPlayerController() const;

	/** The active layout asset (owning ref so a runtime-resolved asset is GC-kept while applied). */
	UPROPERTY()
	TObjectPtr<UHUD_HudLayoutDataAsset> ActiveLayout = nullptr;

	/** Realised slots keyed by slot tag. */
	UPROPERTY()
	TMap<FGameplayTag, FHUD_LiveSlot> LiveSlots;

	/** Streamable manager owning this subsystem's async widget-class loads. */
	FStreamableManager StreamableManager;

	/** Per-slot in-flight class-load handles, keyed by slot tag (so a re-apply cancels stale loads). */
	TMap<FGameplayTag, TSharedPtr<FStreamableHandle>> ActiveStreams;
};
