// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "MessageBus/DPMessage.h"
#include "Containers/Ticker.h"
#include "DPMessageBusSubsystem.generated.h"

/**
 * Typed, GameplayTag-keyed publish/subscribe message bus.
 *
 * - Local (non-replicated) GameInstance subsystem: messages are produced from already-
 *   replicated state and re-broadcast locally; nothing crosses the wire here.
 * - Tag-hierarchy matching: a listener on `DP.Bus.Combat` receives `DP.Bus.Combat.Damage`.
 * - Bridges C++ (TFunction) and Blueprint (dynamic delegate) listeners.
 * - Weak listener ownership: listeners auto-prune when their owning UObject is GC'd.
 * - Deferred dispatch (QueueBroadcast) drains on the next frame via FTSTicker — the
 *   subsystem is NOT itself an FTickableGameObject, avoiding editor/seamless-travel ticking.
 */
UCLASS()
class DESIGNPATTERNS_API UDP_MessageBusSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Native (C++) API ----

	/**
	 * Subscribe a C++ handler. The handler is kept alive by this subsystem but invoked only
	 * while OwnerForLifetime is valid; when that owner is GC'd the entry auto-prunes.
	 */
	FDP_ListenerHandle ListenNative(
		FGameplayTag Channel,
		TFunction<void(const FDP_Message&)> Handler,
		UObject* OwnerForLifetime,
		EDP_MessageMatch Match = EDP_MessageMatch::ExactOrChild);

	/** Broadcast immediately to all matching listeners. */
	void Broadcast(const FDP_Message& Message);

	/** Convenience: build and broadcast a message in one call. */
	void BroadcastPayload(FGameplayTag Channel, const FInstancedStruct& Payload, UObject* Instigator = nullptr);

	/** Queue a message for dispatch on the next frame (avoids reentrancy during a broadcast). */
	void QueueBroadcast(const FDP_Message& Message);

	// ---- Blueprint API ----

	/** Subscribe a Blueprint event. Owner lifetime is taken from the delegate's bound object. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|MessageBus",
		meta = (AdvancedDisplay = "Match"))
	FDP_ListenerHandle ListenBP(
		FGameplayTag Channel,
		FDP_MessageDynamicDelegate Event,
		EDP_MessageMatch Match = EDP_MessageMatch::ExactOrChild);

	/** Broadcast from Blueprint. Build Payload with the engine's Make Instanced Struct node. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|MessageBus")
	void BroadcastBP(FGameplayTag Channel, FInstancedStruct Payload, UObject* Instigator);

	/** Queue a Blueprint broadcast for next-frame dispatch. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|MessageBus")
	void QueueBroadcastBP(FGameplayTag Channel, FInstancedStruct Payload, UObject* Instigator);

	/** Remove a single listener by handle. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|MessageBus")
	void StopListening(FDP_ListenerHandle Handle);

	/** Remove every listener owned by the given object. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|MessageBus")
	void StopListeningForOwner(UObject* Owner);

	/** Number of currently-registered (non-pruned) listeners — used by debug commands/stats. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|MessageBus")
	int32 GetListenerCount() const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Dump every channel and its listeners (owner names) to the log — backing for DP.Bus.DumpListeners. */
	void DumpListeners() const;

private:
	/** One registered listener. Either NativeHandler or BPDelegate is set, never both. */
	struct FListenerEntry
	{
		FDP_ListenerHandle Handle;
		FGameplayTag Channel;
		EDP_MessageMatch Match = EDP_MessageMatch::ExactOrChild;
		TWeakObjectPtr<UObject> Owner;
		TFunction<void(const FDP_Message&)> NativeHandler;
		FDP_MessageDynamicDelegate BPDelegate;
		bool bIsNative = true;
	};

	/** All listeners, grouped by their subscribed channel tag for fast lookup. */
	TMap<FGameplayTag, TArray<TSharedPtr<FListenerEntry>>> Channels;

	/** Handle -> entry, for O(1) StopListening. */
	TMap<FDP_ListenerHandle, TSharedPtr<FListenerEntry>> EntriesByHandle;

	/** Messages awaiting next-frame dispatch. */
	TArray<FDP_Message> DeferredQueue;

	/** FTSTicker handle used to drain the deferred queue. */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Monotonic id source for listener handles. */
	int64 NextHandleId = 1;

	/** True while inside Broadcast, to detect reentrancy and route to the deferred queue. */
	bool bBroadcasting = false;

	FDP_ListenerHandle RegisterEntry(TSharedPtr<FListenerEntry> Entry);
	bool DoesEntryMatch(const FListenerEntry& Entry, const FGameplayTag& BroadcastChannel) const;
	void DrainDeferred();
	bool TickDrain(float DeltaTime);
	void PruneInvalid();
};
