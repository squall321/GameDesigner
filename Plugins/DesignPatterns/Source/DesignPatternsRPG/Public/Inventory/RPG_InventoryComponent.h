// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "RPG_InventoryComponent.generated.h"

class URPG_InventoryComponent;

/**
 * A plain item stack: which item (by identity tag) and how many.
 *
 * The item is referenced by its definition's DataTag, never by a hard pointer, so stacks
 * stay valid across catalog changes and serialize trivially into saves and over the wire.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_ItemStack
{
	GENERATED_BODY()

	/** Identity tag of the item definition (matches URPG_ItemDefinition::DataTag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Inventory")
	FGameplayTag ItemTag;

	/** Number of units in this stack. Always >= 1 while the stack exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Inventory")
	int32 Count = 0;

	FRPG_ItemStack() = default;
	FRPG_ItemStack(const FGameplayTag& InItemTag, int32 InCount)
		: ItemTag(InItemTag), Count(InCount) {}

	bool IsValidStack() const { return ItemTag.IsValid() && Count > 0; }
};

/**
 * One replicated inventory entry. Wraps an FRPG_ItemStack as a fast-array item so adds,
 * count changes and removals delta-replicate individually rather than resending the array.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_InventoryEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The stack carried by this entry. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Inventory")
	FRPG_ItemStack Stack;

	//~ FFastArraySerializerItem replication callbacks (called on clients only).
	void PreReplicatedRemove(const struct FRPG_InventoryArray& InArraySerializer);
	void PostReplicatedAdd(const struct FRPG_InventoryArray& InArraySerializer);
	void PostReplicatedChange(const struct FRPG_InventoryArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the inventory's entries.
 *
 * NetDeltaSerialize forwards to FastArrayDeltaSerialize so only changed entries cross the
 * wire. The owning component pointer is non-replicated and set on the server so the entry
 * callbacks can notify it; on clients it is wired up in the component's ctor too.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_InventoryArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated entries. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Inventory")
	TArray<FRPG_InventoryEntry> Entries;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<URPG_InventoryComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FRPG_InventoryEntry, FRPG_InventoryArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the inventory array. */
template<>
struct TStructOpsTypeTraits<FRPG_InventoryArray> : public TStructOpsTypeTraitsBase2<FRPG_InventoryArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server and clients) whenever the inventory contents change. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPG_OnInventoryChanged, URPG_InventoryComponent*, Inventory);

/**
 * Server-authoritative, replicated item container.
 *
 * Contents replicate via a FFastArraySerializer so individual stack adds/removes/count
 * changes delta-replicate. All mutators (AddItem/RemoveItem/MoveItem) are authority-only:
 * they early-return on non-authority owners, so clients never mutate replicated state and
 * instead observe changes through OnInventoryChanged. Stacking respects each item's
 * MaxStackSize (resolved from the core data registry), overflowing into additional stacks.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_InventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPG_InventoryComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Add Count units of ItemTag, stacking up to the item's MaxStackSize and overflowing into
	 * new stacks as needed. AUTHORITY ONLY. Returns the number of units actually added (always
	 * Count here, as inventory is uncapped; kept as a hook for capacity rules).
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Inventory")
	int32 AddItem(FGameplayTag ItemTag, int32 Count = 1);

	/**
	 * Remove up to Count units of ItemTag across its stacks. AUTHORITY ONLY.
	 * Returns the number of units actually removed (<= Count, capped by what was present).
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Inventory")
	int32 RemoveItem(FGameplayTag ItemTag, int32 Count = 1);

	/**
	 * Move up to Count units of ItemTag from this inventory into Target. AUTHORITY ONLY
	 * (authority of THIS inventory; Target is also mutated on the server). Returns units moved.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Inventory")
	int32 MoveItem(URPG_InventoryComponent* Target, FGameplayTag ItemTag, int32 Count = 1);

	/** Total units of ItemTag across all stacks (read-only; safe on clients). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Inventory")
	int32 GetItemCount(FGameplayTag ItemTag) const;

	/** True if the inventory holds at least Count units of ItemTag. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Inventory")
	bool HasItem(FGameplayTag ItemTag, int32 Count = 1) const { return GetItemCount(ItemTag) >= Count; }

	/** Snapshot of all stacks (read-only copy; safe on clients). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Inventory")
	TArray<FRPG_ItemStack> GetAllStacks() const;

	/** Sum of (per-unit weight * count) over all stacks, using item definitions from the registry. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Inventory")
	float GetTotalWeight() const;

	/**
	 * Designer hook fired after the inventory changes (server and clients). Default
	 * implementation broadcasts OnInventoryChanged; override to extend.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "RPG|Inventory")
	void NotifyInventoryChanged();
	virtual void NotifyInventoryChanged_Implementation();

	/** Broadcast whenever inventory contents change (after replication on clients). */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Inventory")
	FRPG_OnInventoryChanged OnInventoryChanged;

	/** Called by the fast-array entry callbacks on clients to surface a content change. */
	void HandleReplicatedChange();

private:
	/** Replicated item stacks. */
	UPROPERTY(Replicated)
	FRPG_InventoryArray Inventory;

	/** Look up an item's MaxStackSize from the core data registry; 1 if unknown. */
	int32 ResolveMaxStack(const FGameplayTag& ItemTag) const;

	/** Mark an entry dirty and broadcast a change (server side helper). */
	void MarkEntryDirtyAndNotify(FRPG_InventoryEntry& Entry);
};
