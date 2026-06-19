// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "DPUIMessageListener.generated.h"

class UDP_MessageBusSubsystem;

/**
 * Blueprint-assignable callback delegate fired when a subscribed message arrives.
 * Mirrors the core bus's split-parameter signature so no whole FDP_Message or
 * TWeakObjectPtr crosses the Blueprint boundary.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDP_UIMessageReceived,
	FGameplayTag, Channel,
	FInstancedStruct, Payload,
	UObject*, Instigator);

/**
 * A thin, per-view adapter over the CORE message bus (UDP_MessageBusSubsystem).
 *
 * A view (or any UObject) owns one of these to subscribe to a single channel and
 * receive a Blueprint-assignable event when matching messages arrive. The adapter
 * subscribes in Begin() and unsubscribes deterministically in Stop() (and as a
 * backstop in BeginDestroy), so listeners never outlive their owner.
 *
 * This keeps views free of bus plumbing: a view binds OnMessageReceived, calls
 * Begin(Channel), and the adapter handles native subscription/lifetime against
 * the game-instance bus.
 */
UCLASS(BlueprintType, meta = (DisplayName = "DP UI Message Listener"))
class DESIGNPATTERNSUI_API UDP_UIMessageListener : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Construct a ready-to-use listener outered to OwnerForLifetime. The listener's
	 * subscription is tied to that owner on the bus, so it auto-prunes if the owner dies.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|PubSub",
		meta = (WorldContext = "OwnerForLifetime"))
	static UDP_UIMessageListener* CreateUIMessageListener(UObject* OwnerForLifetime);

	/**
	 * Begin listening on Channel. Re-calling with a different channel re-subscribes.
	 * @param Channel  The channel tag to subscribe to.
	 * @param Match    Exact or ExactOrChild (hierarchy) matching.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|PubSub",
		meta = (AdvancedDisplay = "Match"))
	void Begin(FGameplayTag Channel, EDP_MessageMatch Match = EDP_MessageMatch::ExactOrChild);

	/** Stop listening. Deterministic and idempotent. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|PubSub")
	void Stop();

	/** True while an active subscription exists. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|PubSub")
	bool IsListening() const { return Handle.IsValid(); }

	/** The channel currently subscribed to (invalid tag if not listening). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|PubSub")
	FGameplayTag GetChannel() const { return SubscribedChannel; }

	/** Fired (on the game thread) whenever a matching message is received. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|UI|PubSub")
	FDP_UIMessageReceived OnMessageReceived;

protected:
	//~ Begin UObject
	virtual void BeginDestroy() override;
	//~ End UObject

private:
	/** Resolve the core message bus from this listener's owning context. */
	UDP_MessageBusSubsystem* ResolveBus() const;

	/** The bus this listener is registered with, weak so it never keeps the subsystem alive. */
	UPROPERTY()
	TWeakObjectPtr<UDP_MessageBusSubsystem> BoundBus;

	/** Active subscription handle on the bus; invalid when not listening. */
	FDP_ListenerHandle Handle;

	/** The channel currently subscribed to. */
	UPROPERTY()
	FGameplayTag SubscribedChannel;
};
