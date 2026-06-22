// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UObject/WeakInterfacePtr.h"
#include "GameplayTagContainer.h"
#include "Subscriptions/WorldHub_SubscriptionTypes.h"
#include "WorldHub_SubscriptionSubsystem.generated.h"

class UWorldHub_StateHubSubsystem;

/**
 * SCOPED SUBSCRIPTIONS for the world hub.
 *
 * Fine-grained "notify on key X in scope Y" fan-out layered on the single hub OnValueChanged. It binds
 * OnValueChanged ONCE and dispatches to filtered handles by exact key, key tag-parent, scope type, or
 * exact scope (see FWorldHub_SubscriptionFilter). This is a READ-ONLY fan-out and never mutates the
 * hub, so it runs identically on the server AND on clients (no authority required) — clients receive
 * filtered notifications as replicated state arrives via SyncReplicatedState.
 *
 * Subscribe returns an opaque, monotonic FWorldHub_SubscriptionHandle for Unsubscribe; stale handles
 * are inert.
 */
UCLASS()
class DESIGNPATTERNSWORLD_API UWorldHub_SubscriptionSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Register a filtered callback. @return a handle for Unsubscribe, or an invalid handle when the
	 * callback is unbound. Safe on server and client.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Subscription")
	FWorldHub_SubscriptionHandle Subscribe(const FWorldHub_SubscriptionFilter& Filter, const FWorldHub_OnScopedChange& Callback);

	/** Remove a subscription by handle. @return true if a live subscription was removed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Subscription")
	bool Unsubscribe(FWorldHub_SubscriptionHandle Handle);

	/** Remove every subscription whose callback is bound to Object (e.g. an UMG widget being destroyed). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Subscription")
	int32 UnsubscribeAllForObject(UObject* Object);

	/** Number of live subscriptions. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|Subscription")
	int32 GetSubscriptionCount() const { return Subscriptions.Num(); }

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Id -> subscription. UPROPERTY so the dynamic delegate's bound object stays GC-visible. */
	UPROPERTY()
	TMap<int64, FWorldHub_Subscription> Subscriptions;

	/** Monotonic handle id counter (never reused). */
	int64 NextHandleId = 1;

	/** The hub this observes (re-resolved lazily; never owned). */
	TWeakObjectPtr<UWorldHub_StateHubSubsystem> Hub;

	/** Bound once to the hub's OnValueChanged; fans out to matching subscriptions. */
	UFUNCTION()
	void OnHubValueChanged(FWorldHub_Scope Scope, FGameplayTag Key, FSeam_NetValue NewValue);

	/** Resolve / cache the world hub and bind its OnValueChanged. */
	UWorldHub_StateHubSubsystem* ResolveHub();
};
