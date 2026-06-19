// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Object.h"
#include "UObject/WeakObjectPtr.h"
#include "DPServiceTypes.generated.h"

/**
 * Ownership model for a registered service provider.
 *
 * The Service Locator is an OPT-IN convenience over engine-native resolution
 * (GetGameInstance/GetWorld()->GetSubsystem, GetGameInstanceSubsystem, etc.).
 * Prefer engine-native lookup for engine-owned objects; use this locator for
 * cross-cutting, game-authored providers (an interface-typed service object,
 * an actor manager, a save backend) that you want to look up by a stable tag
 * rather than by concrete class.
 */
UENUM(BlueprintType)
enum class EDP_ServiceLifetime : uint8
{
	/**
	 * The locator keeps the provider alive via a strong UPROPERTY reference.
	 * Removal is explicit (UnregisterService) or happens on subsystem teardown.
	 * Use for providers that have no other owner and must outlive their registrant.
	 */
	StrongOwned UMETA(DisplayName = "Strong / Owned"),

	/**
	 * The locator observes the provider via a weak reference and does NOT keep it
	 * alive. If the provider is GC'd the entry auto-invalidates and broadcasts
	 * OnServiceInvalidated. Use for providers owned elsewhere (an actor, a widget,
	 * another subsystem) so registration cannot leak that owner.
	 */
	WeakObserved UMETA(DisplayName = "Weak / Observed")
};

/**
 * Broadcast when a provider is successfully registered (or replaced via override).
 * @param Key      The GameplayTag the provider was registered under.
 * @param Provider The newly-registered provider object (never null at broadcast time).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FDP_OnServiceRegistered, FGameplayTag, Key, UObject*, Provider);

/**
 * Broadcast when a registered service becomes unavailable — either explicitly
 * unregistered, overwritten, or (for WeakObserved) auto-invalidated after its
 * provider was garbage-collected.
 * @param Key The GameplayTag whose binding was invalidated.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDP_OnServiceInvalidated, FGameplayTag, Key);

/**
 * A single registry slot. Holds the provider through EXACTLY ONE of the two pointers,
 * selected by Lifetime: StrongOwned keeps StrongProvider as a GC-rooted UPROPERTY,
 * WeakObserved keeps only WeakProvider so the locator never extends provider lifetime.
 *
 * This is a runtime-only struct (it lives inside a TMap on the subsystem) and is not
 * intended for serialization or Blueprint authoring.
 */
USTRUCT()
struct DESIGNPATTERNS_API FDP_ServiceEntry
{
	GENERATED_BODY()

	/** How this provider is owned; decides which of the two provider pointers is authoritative. */
	UPROPERTY()
	EDP_ServiceLifetime Lifetime = EDP_ServiceLifetime::WeakObserved;

	/** Set iff Lifetime == StrongOwned. A GC-rooting strong reference that keeps the provider alive. */
	UPROPERTY()
	TObjectPtr<UObject> StrongProvider = nullptr;

	/** Set iff Lifetime == WeakObserved. Non-owning; goes stale when the provider is GC'd. */
	UPROPERTY()
	TWeakObjectPtr<UObject> WeakProvider;

	FDP_ServiceEntry() = default;

	/** Returns the live provider for this entry, or null if it is empty / has gone stale. */
	UObject* GetProvider() const
	{
		return (Lifetime == EDP_ServiceLifetime::StrongOwned) ? StrongProvider.Get() : WeakProvider.Get();
	}

	/** True if this slot currently resolves to a live provider. */
	bool IsLive() const
	{
		if (Lifetime == EDP_ServiceLifetime::StrongOwned)
		{
			return StrongProvider != nullptr;
		}
		return WeakProvider.IsValid();
	}
};
