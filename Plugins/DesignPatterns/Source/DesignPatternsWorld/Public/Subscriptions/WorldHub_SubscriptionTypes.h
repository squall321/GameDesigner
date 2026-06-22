// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"
#include "Net/Seam_NetValue.h"
#include "WorldHub_SubscriptionTypes.generated.h"

/** How a subscription's key filter is matched against a changed key. */
UENUM(BlueprintType)
enum class EWorldHub_KeyMatch : uint8
{
	/** Match any key (the KeyFilter is ignored). */
	Any,

	/** Match only the exact KeyFilter tag. */
	Exact,

	/** Match KeyFilter or any of its child tags (FGameplayTag::MatchesTag). */
	TagParent
};

/** How a subscription's scope filter is matched against a changed scope. */
UENUM(BlueprintType)
enum class EWorldHub_ScopeMatch : uint8
{
	/** Match any scope (the ScopeFilter is ignored). */
	Any,

	/** Match only scopes of the same EWorldHub_ScopeType as ScopeFilter. */
	ScopeType,

	/** Match only the exact ScopeFilter (type AND faction/entity). */
	Exact
};

/**
 * An opaque handle returned by Subscribe and accepted by Unsubscribe. Wraps a monotonic int64 so a
 * stale handle can never alias a recycled subscription.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_SubscriptionHandle
{
	GENERATED_BODY()

	/** The monotonic id (0 = invalid / never-subscribed). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Subscription")
	int64 Id = 0;

	FWorldHub_SubscriptionHandle() = default;
	explicit FWorldHub_SubscriptionHandle(int64 InId) : Id(InId) {}

	bool IsValid() const { return Id != 0; }

	bool operator==(const FWorldHub_SubscriptionHandle& Other) const { return Id == Other.Id; }
	friend uint32 GetTypeHash(const FWorldHub_SubscriptionHandle& H) { return GetTypeHash(H.Id); }
};

/**
 * A fine-grained subscription filter: notify on key X in scope Y, with key/scope matching modes that
 * go beyond the single blanket OnValueChanged. All fields are value types (no UObject refs).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_SubscriptionFilter
{
	GENERATED_BODY()

	/** How to match the changed key against KeyFilter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|Subscription")
	EWorldHub_KeyMatch KeyMatch = EWorldHub_KeyMatch::Exact;

	/** The key (or key sub-tree root) to match; ignored when KeyMatch == Any. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|Subscription")
	FGameplayTag KeyFilter;

	/** How to match the changed scope against ScopeFilter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|Subscription")
	EWorldHub_ScopeMatch ScopeMatch = EWorldHub_ScopeMatch::Any;

	/** The scope (or scope type) to match; honored per ScopeMatch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|WorldHub|Subscription")
	FWorldHub_Scope ScopeFilter;

	FWorldHub_SubscriptionFilter() = default;

	/** @return true if a (Scope, Key) change passes this filter. */
	bool Matches(const FWorldHub_Scope& InScope, const FGameplayTag& InKey) const
	{
		// Key gate.
		switch (KeyMatch)
		{
		case EWorldHub_KeyMatch::Exact:     if (InKey != KeyFilter) { return false; } break;
		case EWorldHub_KeyMatch::TagParent: if (!KeyFilter.IsValid() || !InKey.MatchesTag(KeyFilter)) { return false; } break;
		case EWorldHub_KeyMatch::Any:
		default: break;
		}

		// Scope gate.
		switch (ScopeMatch)
		{
		case EWorldHub_ScopeMatch::Exact:     if (InScope != ScopeFilter) { return false; } break;
		case EWorldHub_ScopeMatch::ScopeType: if (InScope.ScopeType != ScopeFilter.ScopeType) { return false; } break;
		case EWorldHub_ScopeMatch::Any:
		default: break;
		}
		return true;
	}
};

/**
 * The callback delegate fired when a subscription's filter passes.
 * @param Scope    The scope whose value changed.
 * @param Key      The key whose value changed.
 * @param NewValue The new net-friendly value (unset on clear / non-net Struct kinds).
 */
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FWorldHub_OnScopedChange, FWorldHub_Scope, Scope, FGameplayTag, Key, FSeam_NetValue, NewValue);

/**
 * One registered subscription (filter + callback). Kept in a UPROPERTY map so the dynamic delegate's
 * bound object stays GC-visible. Not replicated (subscriptions are a LOCAL read-fan-out concern).
 */
USTRUCT()
struct DESIGNPATTERNSWORLD_API FWorldHub_Subscription
{
	GENERATED_BODY()

	/** The filter that gates dispatch. */
	UPROPERTY()
	FWorldHub_SubscriptionFilter Filter;

	/** The callback invoked when the filter passes. */
	UPROPERTY()
	FWorldHub_OnScopedChange Callback;

	FWorldHub_Subscription() = default;
};
