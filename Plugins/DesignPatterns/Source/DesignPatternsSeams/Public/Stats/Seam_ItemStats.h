// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Stats/Seam_StatMod.h"
#include "Seam_ItemStats.generated.h"

/**
 * The resolved stat contribution of a single item tag, in seam-neutral terms.
 *
 * Placed beside Seam_StatMod.h under Stats/ because it composes the EXISTING FSeam_StatMod
 * (AttributeTag / Op / Magnitude(FSeam_NetValue) / SourceTag): an item resolves to an ordered
 * list of the stat modifiers it grants. The numeric magnitude of a modifier is read from
 * Mods[i].Magnitude.FloatValue (a public BlueprintReadOnly field on FSeam_NetValue), so a
 * comparison UI can diff two items' contributions without depending on the RPG stats module.
 *
 * bResolved distinguishes "this item grants no stats" (Mods empty, bResolved true) from
 * "we could not resolve this item" (Mods empty, bResolved false), mirroring the bResolved
 * contract on FInvUI_ItemDisplayInfo so a comparison view can clear pending state correctly.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_ItemStatSet
{
	GENERATED_BODY()

	/** The item this set describes (echoed back for async correlation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|ItemStats")
	FGameplayTag ItemTag;

	/** Stat modifiers the item contributes, in the resolver's order. May be empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|ItemStats")
	TArray<FSeam_StatMod> Mods;

	/** True when the item was successfully resolved (false = unknown item / resolve failed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|ItemStats")
	bool bResolved = false;

	FSeam_ItemStatSet() = default;

	/**
	 * Sum the magnitudes of every modifier targeting AttributeTag (numeric/float magnitudes only).
	 * A convenience for a comparison view that wants the net per-attribute contribution rather
	 * than the individual modifier list. Non-numeric magnitudes contribute 0.
	 */
	double GetAttributeTotal(const FGameplayTag& AttributeTag) const
	{
		double Total = 0.0;
		for (const FSeam_StatMod& Mod : Mods)
		{
			if (Mod.AttributeTag == AttributeTag)
			{
				Total += Mod.Magnitude.FloatValue;
			}
		}
		return Total;
	}
};

/** Async result delegate: fired once per ResolveItemStats call with the populated set. */
DECLARE_DYNAMIC_DELEGATE_OneParam(FSeam_OnItemStatsResolved, const FSeam_ItemStatSet&, Stats);

UINTERFACE(MinimalAPI, BlueprintType, meta = (DisplayName = "Seam Item Stats"))
class USeam_ItemStats : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam that resolves an opaque item tag to the set of stat modifiers it grants, asynchronously.
 *
 * A genre module (usually the RPG/items module) implements this — typically by looking the tag
 * up in the core data registry and reading an item-definition data asset — and publishes it
 * under a project service-locator key so a comparison tooltip can read stat deltas WITHOUT
 * coupling the inventory UI to the concrete stats system. Mirrors IInvUI_ItemDisplay's
 * async + synchronous-cached contract exactly, so a consumer can paint instantly when the data
 * is resident and fall back to the async path otherwise.
 *
 * Read-only by design: it never mutates an item or a stats component. All methods are
 * BlueprintNativeEvent so consumers call them through the generated Execute_ thunks and a
 * project can author a resolver in Blueprint.
 */
class ISeam_ItemStats
{
	GENERATED_BODY()

public:
	/**
	 * Synchronous fast path: if the stat set for ItemTag is already cached/resident, fill Out
	 * (with bResolved set appropriately) and return true. Returns false (leaving Out default)
	 * when a resolve would have to load — the caller should then use ResolveItemStats.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|ItemStats")
	bool TryGetItemStats(FGameplayTag ItemTag, FSeam_ItemStatSet& Out) const;

	/**
	 * Begin resolving the stat set for ItemTag. OnResolved is invoked exactly once — possibly
	 * synchronously, possibly on a later frame after a load completes. Implementations must still
	 * fire OnResolved (with bResolved=false) for an unknown tag so callers can clear pending state.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|ItemStats")
	void ResolveItemStats(FGameplayTag ItemTag, const FSeam_OnItemStatsResolved& OnResolved);
};
