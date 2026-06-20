// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Containers/Ticker.h"
#include "Notification/HUD_NotificationTypes.h"
#include "HUD_NotificationSubsystem.generated.h"

class UHUD_NotificationViewModel;
class UHUD_NotificationMapDataAsset;
class UDP_MessageBusSubsystem;
class IHUD_NotificationSource;
struct FDP_Message;
struct FHUD_ActiveNotificationView;

/**
 * Local-player-scoped notification / toast manager.
 *
 * Responsibilities:
 *  - PRIORITY QUEUE of toasts/banners: higher Priority shows first; the on-screen set is capped at
 *    MaxOnScreen and lower-priority items wait in the queue (FIFO within a priority).
 *  - DE-DUPLICATION by FHUD_Notification::DedupeKey: pushing a key that is already active/queued
 *    refreshes it in place instead of stacking duplicates.
 *  - BUS-DRIVEN surfacing: reads a data-driven UHUD_NotificationMapDataAsset and subscribes (via
 *    ListenNative) to each mapped DP.Bus.* channel, turning already-replicated gameplay events into
 *    notifications with zero producer coupling. Also listens on DP.Bus.HUD.Notify / .Dismiss for
 *    direct payload-driven requests.
 *  - SOURCE registration: optional IHUD_NotificationSource producers (push-only) bound through the
 *    service locator key DP.Service.HUD.NotificationSource.
 *  - VIEWMODEL exposure: owns a UHUD_NotificationViewModel the UI binds; pushes the visible set and
 *    counts whenever the queue changes or timers tick.
 *
 * LOCAL ONLY — nothing here replicates; it is a cosmetic projection of replicated gameplay.
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_NotificationSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Push a notification request. De-duplicates by DedupeKey, inserts by Priority, and surfaces it
	 * immediately if there is room on screen; otherwise it waits in the priority queue.
	 * @return the instance id assigned to the (new or refreshed) notification.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Notification")
	int64 PushNotification(const FHUD_Notification& Notification);

	/**
	 * Dismiss every active/queued notification whose DedupeKey matches Key (hierarchy-aware: a
	 * dismiss on DP.HUD.Key.Quest removes DP.HUD.Key.Quest.Main). No-op if none match.
	 * @return number of notifications dismissed.
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Notification")
	int32 DismissByKey(FGameplayTag Key);

	/** Dismiss a single active/queued notification by its instance id. @return true if found. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Notification")
	bool DismissByInstanceId(int64 InstanceId);

	/** Clear every active and queued notification immediately. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Notification")
	void ClearAll();

	/** The ViewModel the UI binds to (always valid after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "HUD|Notification")
	UHUD_NotificationViewModel* GetViewModel() const { return ViewModel; }

	/**
	 * Set the active notification map (which bus channels surface which notifications). Re-subscribes
	 * to the new map's channels. Normally resolved from settings/registry, but exposed for runtime
	 * swap. Passing null detaches all map-driven subscriptions (direct/bus-payload pushes still work).
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Notification")
	void SetNotificationMap(UHUD_NotificationMapDataAsset* InMap);

	/** Resolve MapTag through the data registry and SetNotificationMap. @return true if applied. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Notification")
	bool SetNotificationMapByTag(FGameplayTag MapTag);

	/**
	 * Register a push-only notification source. The source is bound (OnNotificationSinkBound) and any
	 * buffered notifications it drains are enqueued. Strongly referenced until unregistered; safe to
	 * register the same source twice (idempotent).
	 */
	UFUNCTION(BlueprintCallable, Category = "HUD|Notification")
	void RegisterSource(const TScriptInterface<IHUD_NotificationSource>& Source);

	/** Unregister a previously-registered source (OnNotificationSinkUnbound is called). */
	UFUNCTION(BlueprintCallable, Category = "HUD|Notification")
	void UnregisterSource(const TScriptInterface<IHUD_NotificationSource>& Source);

	/** Maximum notifications shown on screen at once (defensive default 3; tune in settings). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "HUD|Notification")
	int32 GetMaxOnScreen() const { return MaxOnScreen; }

	/** Set the on-screen cap and re-promote queued items if it grew. */
	UFUNCTION(BlueprintCallable, Category = "HUD|Notification")
	void SetMaxOnScreen(int32 InMaxOnScreen);

	/** Append a multi-line dump of the active + queued notifications to OutLines. */
	void DumpTo(TArray<FString>& OutLines) const;

private:
	/** A queued or active notification with its bookkeeping. */
	struct FNotificationItem
	{
		/** Monotonic instance id. */
		int64 InstanceId = 0;
		/** The notification content. */
		FHUD_Notification Notification;
		/** Seconds left on screen (only meaningful while active and non-sticky). */
		float TimeRemaining = 0.f;
		/** True while this item is currently on screen (counted against MaxOnScreen). */
		bool bActive = false;
	};

	/** Resolve the game-instance message bus for this local player, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** The owning local player's player controller (world-context), or null. */
	APlayerController* GetOwningPlayerController() const;

	/** (Re)subscribe to the active map's bus channels + the HUD notify/dismiss channels. */
	void RefreshBusSubscriptions();

	/** Bus handler for a map-mapped or HUD notify/dismiss channel. */
	void HandleBusMessage(const FDP_Message& Message);

	/** Insert Item into Items ordered by descending priority (stable within a priority). */
	void InsertByPriority(FNotificationItem&& Item);

	/** Find an item (active or queued) by dedupe key; null if none. Key must be valid. */
	FNotificationItem* FindByDedupeKey(const FGameplayTag& Key);

	/** Find an item by instance id; null if none. */
	int32 IndexOfInstance(int64 InstanceId) const;

	/** Promote queued items into the active set up to MaxOnScreen; starts their timers. */
	void PromoteQueued();

	/** Per-frame timer tick: decrement active durations and dismiss expired non-sticky items. */
	bool TickNotifications(float DeltaTime);

	/** Rebuild the ViewModel's visible set + counts from the current item list. */
	void PushToViewModel();

	/** Default category fallback applied to a pushed notification with an unset Category. */
	void NormalizeNotification(FHUD_Notification& InOut) const;

	/** The ViewModel the UI binds to. Owning ref keeps it alive. */
	UPROPERTY()
	TObjectPtr<UHUD_NotificationViewModel> ViewModel = nullptr;

	/** The active bus-channel -> notification map (owning ref so a resolved asset stays loaded). */
	UPROPERTY()
	TObjectPtr<UHUD_NotificationMapDataAsset> NotificationMap = nullptr;

	/**
	 * Registered push-only sources. A UPROPERTY TScriptInterface is a STRONG ref, so a registered
	 * source is GC-kept until UnregisterSource / Deinitialize. Sources are expected to unregister in
	 * their own teardown; the subsystem also unbinds every source on Deinitialize.
	 */
	UPROPERTY()
	TArray<TScriptInterface<IHUD_NotificationSource>> Sources;

	/**
	 * All notifications, ordered by descending priority (index 0 = highest). The first up-to
	 * MaxOnScreen items flagged bActive are on screen; the rest are queued. Not a UPROPERTY: holds no
	 * UObject refs (FHUD_Notification is a plain value struct) so GC has nothing to track.
	 */
	TArray<FNotificationItem> Items;

	/**
	 * Maximum simultaneously-visible notifications. Defensive fallback default (documented) used when
	 * no project settings override is present; designers tune via SetMaxOnScreen / future settings CDO.
	 */
	UPROPERTY(EditAnywhere, Category = "HUD|Notification", meta = (ClampMin = "1"))
	int32 MaxOnScreen = 3;

	/** Monotonic instance-id source. */
	int64 NextInstanceId = 1;

	/** FTSTicker handle draining notification timers each frame. */
	FTSTicker::FDelegateHandle TickerHandle;
};
