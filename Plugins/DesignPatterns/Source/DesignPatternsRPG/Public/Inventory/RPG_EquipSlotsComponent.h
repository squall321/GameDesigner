// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Inventory/RPG_EquipmentComponent.h"
#include "RPG_EquipSlotsComponent.generated.h"

class URPG_EquipSlotsComponent;
class URPG_ItemInstanceComponent;
class URPG_EquipSlotLayout;
class URPG_EquipmentSetDefinition;
class ISeam_StatModifierSink;

/**
 * One replicated SlotTag -> InstanceId binding on the equip-slots subclass.
 *
 * The base FRPG_EquipmentEntry maps a slot to an ITEM TAG, which cannot disambiguate two distinct rolled
 * instances of the same definition. This parallel binding carries the stable InstanceId for each slot so the
 * subclass can resolve the exact rolled item (its affixes/sockets) WITHOUT modifying the base entry.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_EquipSlotInstanceEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Which slot this binding is for. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Slots")
	FGameplayTag SlotTag;

	/** Stable instance id of the item equipped in SlotTag (0 = none / a base item without an instance). */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Slots")
	int32 InstanceId = 0;

	//~ FFastArraySerializerItem replication callbacks (client side).
	void PreReplicatedRemove(const struct FRPG_EquipSlotInstanceArray& InArraySerializer);
	void PostReplicatedAdd(const struct FRPG_EquipSlotInstanceArray& InArraySerializer);
	void PostReplicatedChange(const struct FRPG_EquipSlotInstanceArray& InArraySerializer);
};

/** Fast-array serializer holding the slot->instance bindings. Identical shape to the base equipment array. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_EquipSlotInstanceArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated bindings. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Slots")
	TArray<FRPG_EquipSlotInstanceEntry> Entries;

	/** Non-replicated back-pointer to the owning component. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<URPG_EquipSlotsComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FRPG_EquipSlotInstanceEntry, FRPG_EquipSlotInstanceArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the slot-instance binding array. */
template<>
struct TStructOpsTypeTraits<FRPG_EquipSlotInstanceArray> : public TStructOpsTypeTraitsBase2<FRPG_EquipSlotInstanceArray>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * Equipment component with typed slots, sockets, set bonuses and stat application.
 *
 * It extends URPG_EquipmentComponent purely additively:
 *  - EquipInstance validates the item's ItemTypeTag against a data-driven URPG_EquipSlotLayout, then calls the
 *    base Equip and records the stable InstanceId in a parallel replicated binding.
 *  - NotifyEquipmentChanged_Implementation overrides the base hook, calling Super FIRST (so the base
 *    OnEquipmentChanged still fires for UI), then RecomputeSlotModifiers.
 *  - RecomputeSlotModifiers runs on SERVER AND CLIENTS with NO authority guard — it is PURE LOCAL DERIVATION
 *    from already-replicated state (equipment slots + item instances). It maps each equipped item's affixes
 *    and socketed gems to FRPG_StatModifier and pushes them through the LOCAL ISeam_StatModifierSink path
 *    SetDerivedModifierGroup, keyed by a per-slot SourceTag; active set bonuses go through the same local
 *    path under a set SourceTag. This is the dual-path stat rule: equipment/affix/set modifiers must NEVER
 *    funnel through the authority-only AddModifierBatch, or they would vanish on clients.
 *
 * It binds the sibling URPG_ItemInstanceComponent::OnInstancesChanged so a late-arriving instance triggers a
 * recompute, making the two fast-arrays converge regardless of replication order.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_EquipSlotsComponent : public URPG_EquipmentComponent
{
	GENERATED_BODY()

public:
	URPG_EquipSlotsComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Equip a specific rolled instance into SlotTag. AUTHORITY ONLY. Validates the instance's item type
	 * against SlotLayout, calls the base Equip with the item's tag, and records the InstanceId binding.
	 * Returns true on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Slots")
	bool EquipInstance(FGameplayTag SlotTag, int32 InstanceId);

	/** Clear a slot and its instance binding. AUTHORITY ONLY. Returns true if the slot was occupied. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Slots")
	bool UnequipSlot(FGameplayTag SlotTag);

	/** Instance id bound to SlotTag, or 0 if none. Safe on clients. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Slots")
	int32 GetEquippedInstanceId(FGameplayTag SlotTag) const;

	/**
	 * Socket GemItemTag into socket SocketIndex of the instance equipped in SlotTag. AUTHORITY ONLY.
	 * Returns true on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Slots")
	bool SocketGem(FGameplayTag SlotTag, int32 SocketIndex, FGameplayTag GemItemTag);

	/**
	 * Remove the gem from socket SocketIndex of the instance equipped in SlotTag. AUTHORITY ONLY. When
	 * bReturnToInventory is true and the owner has an inventory, the removed gem is added back to it.
	 * Returns true on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Slots")
	bool UnsocketGem(FGameplayTag SlotTag, int32 SocketIndex, bool bReturnToInventory);

	/** Data-driven typed-slot layout (validates EquipInstance). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Slots")
	TObjectPtr<URPG_EquipSlotLayout> SlotLayout;

	/** Equipment sets evaluated for set bonuses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Slots")
	TArray<TObjectPtr<URPG_EquipmentSetDefinition>> KnownSets;

	/**
	 * Recompute and republish the derived modifier group for SlotTag (affixes + socketed gems of the
	 * equipped instance) AND the aggregate set-bonus group. Runs on server AND clients (local derivation).
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Slots")
	void RecomputeSlotModifiers(FGameplayTag SlotTag);

	//~ Begin URPG_EquipmentComponent
	/** Calls Super FIRST (preserves base OnEquipmentChanged), then recomputes the slot's modifiers. */
	virtual void NotifyEquipmentChanged_Implementation(FGameplayTag SlotTag) override;
	//~ End URPG_EquipmentComponent

protected:
	/** Resolve the stat-modifier sink (URPG_StatsComponent) off the owning actor. May be null. */
	TScriptInterface<ISeam_StatModifierSink> ResolveStatSink() const;

	/** Resolve the sibling instance carrier off the owning actor. May be null. */
	URPG_ItemInstanceComponent* ResolveInstanceComponent() const;

	/** Bound to the instance carrier's change delegate to re-resolve all slot modifiers. */
	UFUNCTION()
	void HandleInstancesChanged(URPG_ItemInstanceComponent* Component);

	/** Recompute set bonuses across all equipped instances and push them via the local sink path. */
	void RecomputeSetBonuses();

	/** Build the per-slot SourceTag used for that slot's derived modifier group. */
	FGameplayTag MakeSlotSourceTag(const FGameplayTag& SlotTag) const;

private:
	/** Parallel replicated SlotTag->InstanceId binding. */
	UPROPERTY(Replicated)
	FRPG_EquipSlotInstanceArray SlotInstanceBindings;

	/** Cached sink so a recompute does not re-resolve every frame; refreshed lazily. */
	UPROPERTY(Transient)
	TScriptInterface<ISeam_StatModifierSink> CachedStatSink;

	/** Whether we have already bound the instance-carrier delegate. */
	bool bBoundInstanceDelegate = false;

	/** Find the binding index for SlotTag, or INDEX_NONE. */
	int32 FindBindingIndex(const FGameplayTag& SlotTag) const;

	/** Set/clear the InstanceId bound to SlotTag (AUTHORITY; marks the binding dirty). */
	void SetBinding(const FGameplayTag& SlotTag, int32 InstanceId);
};
