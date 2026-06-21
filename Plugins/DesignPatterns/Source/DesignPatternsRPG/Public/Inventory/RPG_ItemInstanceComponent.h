// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Persist/Seam_Persistable.h"
#include "Item/RPG_ItemInstance.h"
#include "RPG_ItemInstanceComponent.generated.h"

class URPG_ItemInstanceComponent;

/**
 * One replicated entry wrapping a single FRPG_ItemInstance.
 *
 * Mirrors FRPG_InventoryEntry exactly: the delta callbacks (called on CLIENTS after replication) route back
 * through the owning component so any equipment/encumbrance recompute that depends on instance data
 * converges regardless of which fast-array (stack inventory vs. instances vs. slot bindings) replicates
 * first.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_ItemInstanceEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The per-instance item state carried by this entry. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item")
	FRPG_ItemInstance Instance;

	//~ FFastArraySerializerItem replication callbacks (client side).
	void PreReplicatedRemove(const struct FRPG_ItemInstanceArray& InArraySerializer);
	void PostReplicatedAdd(const struct FRPG_ItemInstanceArray& InArraySerializer);
	void PostReplicatedChange(const struct FRPG_ItemInstanceArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the item instances. NetDeltaSerialize forwards to FastArrayDeltaSerialize so
 * only changed instances cross the wire; the OwnerComponent back-pointer is non-replicated and wired up in
 * the component ctor (server and client) so the entry callbacks can notify it. Identical shape to
 * FRPG_InventoryArray.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_ItemInstanceArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated instance entries. */
	UPROPERTY(BlueprintReadOnly, Category = "RPG|Item")
	TArray<FRPG_ItemInstanceEntry> Entries;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<URPG_ItemInstanceComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FRPG_ItemInstanceEntry, FRPG_ItemInstanceArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the instance array. */
template<>
struct TStructOpsTypeTraits<FRPG_ItemInstanceArray> : public TStructOpsTypeTraitsBase2<FRPG_ItemInstanceArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server and clients) whenever any item instance is added, removed or mutated. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRPG_OnItemInstancesChanged, URPG_ItemInstanceComponent*, Component);

/**
 * Durable save record for the instance component: the full instance list plus the monotonic id source, so
 * stable InstanceIds survive a save/load round-trip and never collide with future adds.
 */
USTRUCT()
struct DESIGNPATTERNSRPG_API FRPG_ItemInstanceSaveData
{
	GENERATED_BODY()

	/** Every persisted instance verbatim (affixes, sockets, durability, ids). */
	UPROPERTY(SaveGame)
	TArray<FRPG_ItemInstance> Instances;

	/** The next id to hand out, persisted so re-adds after load do not reuse a live id. */
	UPROPERTY(SaveGame)
	int32 NextInstanceId = 1;
};

/**
 * Server-authoritative carrier for non-stackable rolled gear (the per-instance counterpart to the stackable
 * URPG_InventoryComponent). Each FRPG_ItemInstance has a stable InstanceId, assigned monotonically on add
 * from a server-only, persisted NextInstanceId, so equipment slot bindings and saves can address a specific
 * rolled item even when two share an ItemTag.
 *
 * All mutators are AUTHORITY-ONLY (guarded at the TOP); clients observe changes through the replicated
 * fast-array and OnInstancesChanged. The component persists its instances and NextInstanceId through the
 * existing versioned save via ISeam_Persistable — no new save system.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_ItemInstanceComponent : public UActorComponent, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	URPG_ItemInstanceComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/**
	 * Add a rolled instance, assigning it a fresh stable InstanceId (overwriting any inbound id). AUTHORITY
	 * ONLY. Returns the assigned InstanceId, or 0 on failure (non-authority / invalid instance).
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item")
	int32 AddInstance(const FRPG_ItemInstance& Instance);

	/** Remove the instance with InstanceId. AUTHORITY ONLY. Returns true if one was removed. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item")
	bool RemoveInstance(int32 InstanceId);

	/** Copy the instance with InstanceId into Out. Returns false if not found. Safe on clients. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item")
	bool GetInstance(int32 InstanceId, FRPG_ItemInstance& Out) const;

	/** Snapshot of all instances (read-only copy). Safe on clients. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item")
	TArray<FRPG_ItemInstance> GetAllInstances() const;

	/** True if an instance with InstanceId exists. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Item")
	bool HasInstance(int32 InstanceId) const;

	/**
	 * Mutate the instance with InstanceId in place via Mutator, marking the entry dirty and notifying.
	 * AUTHORITY ONLY. Returns true if the instance was found and mutated.
	 */
	bool MutateInstance(int32 InstanceId, TFunctionRef<void(FRPG_ItemInstance&)> Mutator);

	/** Broadcast on add/remove/change (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Item")
	FRPG_OnItemInstancesChanged OnInstancesChanged;

	/** Called by the fast-array entry callbacks on clients to surface a change. */
	void HandleReplicatedChange();

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

protected:
	/** Notify listeners that instances changed (server side helper + client surface). */
	void NotifyInstancesChanged();

private:
	/** Replicated instance entries. */
	UPROPERTY(Replicated)
	FRPG_ItemInstanceArray Instances;

	/** Server-only, persisted monotonic id source. Starts at 1 so 0 means "invalid/unset". */
	UPROPERTY()
	int32 NextInstanceId = 1;

	/** Index of the entry for InstanceId, or INDEX_NONE. */
	int32 FindEntryIndex(int32 InstanceId) const;
};
