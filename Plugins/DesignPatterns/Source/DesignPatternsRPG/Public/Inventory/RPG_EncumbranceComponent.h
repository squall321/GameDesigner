// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "RPG_EncumbranceComponent.generated.h"

class URPG_EncumbranceCurve;
class URPG_InventoryComponent;
class URPG_ItemInstanceComponent;
class ISeam_StatModifierSink;

/** Broadcast (server and clients) when the active encumbrance tier changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRPG_OnEncumbranceChanged, URPG_EncumbranceComponent*, Component, FGameplayTag, TierTag);

/**
 * Derives an over-encumbrance movement penalty from carried weight.
 *
 * Carried weight is the sum of the stackable inventory's GetTotalWeight() and the per-item weight of every
 * rolled instance the owner carries; capacity is BaseCapacity plus a strength bonus read through the stats
 * seam. The carried/capacity fraction selects an encumbrance tier from the data asset, and the tier's
 * move-speed penalty is published through the LOCAL ISeam_StatModifierSink path SetDerivedModifierGroup —
 * NOT the authority-only path — so the penalty exists identically on server and clients (the dual-path stat
 * rule). Recalculation is bound to OnInventoryChanged (which fires on both sides), so weight changes
 * automatically re-derive the penalty.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_EncumbranceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPG_EncumbranceComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** Total carried weight (stackable inventory + per-instance item weights). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Encumbrance")
	float GetCarriedWeight() const;

	/** Carry capacity (BaseCapacity + strength bonus from the curve). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Encumbrance")
	float GetCapacity() const;

	/** The currently active encumbrance tier tag (empty when unencumbered). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Encumbrance")
	FGameplayTag GetEncumbranceTier() const { return CurrentTier; }

	/**
	 * Recompute carried weight vs. capacity, resolve the active tier and republish the move-speed penalty
	 * group via the local sink path. Runs on server AND clients (local derivation). Bound to
	 * OnInventoryChanged; may also be called directly (e.g. after equipping a heavy instance).
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Encumbrance")
	void RecalculateEncumbrance();

	/** Data-driven capacity + tier thresholds + penalties. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Encumbrance")
	TObjectPtr<URPG_EncumbranceCurve> Tiers;

	/** Broadcast when the active tier changes. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Encumbrance")
	FRPG_OnEncumbranceChanged OnEncumbranceChanged;

protected:
	/** Resolve the stat-modifier sink off the owning actor. May be null. */
	TScriptInterface<ISeam_StatModifierSink> ResolveStatSink() const;

	/** Resolve the stackable inventory off the owning actor. May be null. */
	URPG_InventoryComponent* ResolveInventory() const;

	/** Resolve the per-instance carrier off the owning actor. May be null. */
	URPG_ItemInstanceComponent* ResolveInstanceComponent() const;

	/** Read the owner's Strength attribute via the stats seam (0 if unavailable). */
	float ResolveStrength() const;

	/** Bound to the inventory's change delegate. */
	UFUNCTION()
	void HandleInventoryChanged(URPG_InventoryComponent* Inventory);

	/** Bound to the instance carrier's change delegate. */
	UFUNCTION()
	void HandleInstancesChanged(URPG_ItemInstanceComponent* Component);

private:
	/** Last published tier (so we only broadcast on change). */
	UPROPERTY(Transient)
	FGameplayTag CurrentTier;

	/** Whether change delegates have been bound. */
	bool bBoundDelegates = false;
};
