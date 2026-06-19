// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "RPG_EquipmentComponent.generated.h"

class URPG_EquipmentComponent;

/**
 * One replicated equipped item: a slot tag (e.g. "RPG.Slot.Weapon") bound to an item
 * identity tag (matches URPG_ItemDefinition::DataTag). Replicated as a fast-array item so
 * individual equip/unequip events delta-replicate.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_EquipmentEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Which equipment slot this entry occupies. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Equipment")
	FGameplayTag SlotTag;

	/** The item identity tag equipped in this slot. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Equipment")
	FGameplayTag ItemTag;

	//~ FFastArraySerializerItem replication callbacks (client side).
	void PreReplicatedRemove(const struct FRPG_EquipmentArray& InArraySerializer);
	void PostReplicatedAdd(const struct FRPG_EquipmentArray& InArraySerializer);
	void PostReplicatedChange(const struct FRPG_EquipmentArray& InArraySerializer);
};

/** Fast-array serializer holding equipped slots. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_EquipmentArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated equipped entries. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Equipment")
	TArray<FRPG_EquipmentEntry> Entries;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<URPG_EquipmentComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FRPG_EquipmentEntry, FRPG_EquipmentArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the equipment array. */
template<>
struct TStructOpsTypeTraits<FRPG_EquipmentArray> : public TStructOpsTypeTraitsBase2<FRPG_EquipmentArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server and clients) whenever equipment changes; carries the affected slot. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRPG_OnEquipmentChanged, URPG_EquipmentComponent*, Equipment, FGameplayTag, SlotTag);

/**
 * Server-authoritative, replicated equipment slots keyed by FGameplayTag.
 *
 * Slots map a slot tag (e.g. "RPG.Slot.Weapon") to an item identity tag. Equip/Unequip are
 * authority-only and early-return on clients; clients observe slot changes through
 * OnEquipmentChanged via the replicated fast array.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_EquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPG_EquipmentComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Equip ItemTag into SlotTag, replacing any item already there. AUTHORITY ONLY.
	 * Returns true if the slot's contents changed.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Equipment")
	bool Equip(FGameplayTag SlotTag, FGameplayTag ItemTag);

	/** Clear SlotTag. AUTHORITY ONLY. Returns true if the slot was occupied. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Equipment")
	bool Unequip(FGameplayTag SlotTag);

	/** Item identity tag currently in SlotTag, or an empty tag if the slot is free. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Equipment")
	FGameplayTag GetEquippedItem(FGameplayTag SlotTag) const;

	/** True if SlotTag currently holds an item. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Equipment")
	bool IsSlotOccupied(FGameplayTag SlotTag) const { return GetEquippedItem(SlotTag).IsValid(); }

	/** Every occupied slot tag. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Equipment")
	TArray<FGameplayTag> GetOccupiedSlots() const;

	/**
	 * Designer hook fired after a slot changes (server and clients). Default broadcasts
	 * OnEquipmentChanged; override to extend (e.g. apply stat modifiers).
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "RPG|Equipment")
	void NotifyEquipmentChanged(FGameplayTag SlotTag);
	virtual void NotifyEquipmentChanged_Implementation(FGameplayTag SlotTag);

	/** Broadcast whenever a slot changes (after replication on clients). */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Equipment")
	FRPG_OnEquipmentChanged OnEquipmentChanged;

	/** Called by the fast-array entry callbacks on clients to surface a slot change. */
	void HandleReplicatedChange(FGameplayTag SlotTag);

private:
	/** Replicated equipment slots. */
	UPROPERTY(Replicated)
	FRPG_EquipmentArray Equipment;

	/** Index of the entry for SlotTag, or INDEX_NONE. */
	int32 FindSlotIndex(const FGameplayTag& SlotTag) const;
};
