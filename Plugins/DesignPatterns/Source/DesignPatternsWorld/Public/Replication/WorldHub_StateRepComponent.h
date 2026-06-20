// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Hub/WorldHub_Scope.h"
#include "Registry/WorldHub_FlagRegistry.h"
#include "WorldHub_StateRepComponent.generated.h"

class UWorldHub_StateRepComponent;
class UWorldHub_StateHubSubsystem;

/**
 * One replicated world-state entry: a (Scope, Key) address and its net-friendly value.
 *
 * Wrapped as a fast-array item so a single flag flip / counter change / value set delta-replicates
 * individually rather than resending the whole table. The scope rides ALONGSIDE the (Key, Value)
 * pair here (the types-area FWorldHub_RepStateEntry documents that the enclosing fast-array item
 * carries the scope). FSeam_NetValue is the only arbitrary value allowed across the wire and brings
 * its own compact NetSerialize. The per-item callbacks (client side) push the change into the local
 * hub's registry and fire its OnValueChanged.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_RepEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The scope this value belongs to (Global / Faction / Entity). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub")
	FWorldHub_Scope Scope;

	/** The flag key this value belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub")
	FGameplayTag Key;

	/** The net-friendly value for (Scope, Key). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub")
	FSeam_NetValue Value;

	FWorldHub_RepEntry() = default;
	FWorldHub_RepEntry(const FWorldHub_Scope& InScope, const FGameplayTag& InKey, const FSeam_NetValue& InValue)
		: Scope(InScope), Key(InKey), Value(InValue) {}

	//~ FFastArraySerializerItem replication callbacks (called on clients only).
	void PreReplicatedRemove(const struct FWorldHub_RepStateArray& InArraySerializer);
	void PostReplicatedAdd(const struct FWorldHub_RepStateArray& InArraySerializer);
	void PostReplicatedChange(const struct FWorldHub_RepStateArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the world-state entries.
 *
 * NetDeltaSerialize forwards to FastArrayDeltaSerialize so only changed entries cross the wire. The
 * owning-component back-pointer is non-replicated and set in the component ctor so the per-entry
 * client callbacks can locate the component and forward the change to the local hub. Per-entry
 * callbacks additionally tell the array which key changed so the component can fire a precise
 * OnValueChanged rather than a blanket resync.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_RepStateArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated entries. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub")
	TArray<FWorldHub_RepEntry> Entries;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UWorldHub_StateRepComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FWorldHub_RepEntry, FWorldHub_RepStateArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the world-state array. */
template<>
struct TStructOpsTypeTraits<FWorldHub_RepStateArray> : public TStructOpsTypeTraitsBase2<FWorldHub_RepStateArray>
{
	enum { WithNetDeltaSerializer = true };
};

/**
 * The replicated carrier for world-hub net state.
 *
 * The world-hub subsystem is NEVER replicated, so authoritative net-friendly entries are mirrored
 * onto THIS actor component (placed on a stable, always-relevant replicated actor such as the
 * GameState or a dedicated AInfo). The fast array delta-replicates entries to every client, whose
 * carrier then pushes them into that client's local hub registry and fires OnValueChanged.
 *
 * All mutators are AUTHORITY ONLY (guarded at the TOP). Client-driven intent must route through a
 * PLAYER-OWNED component with a validated Server RPC that calls the hub's server-side authority API
 * (which re-derives/re-checks the request); it must never mutate this carrier from a client.
 *
 * On BeginPlay the carrier resolves the local hub and attaches itself (AttachNetCarrier) so the hub
 * mirrors subsequent authoritative writes here, and on the authority it seeds itself from the hub's
 * current replicable state.
 */
UCLASS(ClassGroup = (DesignPatternsWorld), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSWORLD_API UWorldHub_StateRepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWorldHub_StateRepComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * Authoritatively set (or update) the entry for (Scope, Key). AUTHORITY ONLY: no-op on clients.
	 * @return true if an entry was added or its value changed.
	 */
	bool Authority_SetEntry(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FSeam_NetValue& Value);

	/** Authoritatively remove (Scope, Key)'s entry. AUTHORITY ONLY: no-op on clients. @return true if removed. */
	bool Authority_RemoveEntry(const FWorldHub_Scope& Scope, const FGameplayTag& Key);

	/**
	 * Authoritatively replace the entire entry set from the registry's replicable snapshot (used to
	 * seed the carrier from the hub on BeginPlay). AUTHORITY ONLY.
	 */
	void Authority_ReplaceAll(const TArray<FWorldHub_ScopedRepEntry>& NewEntries);

	/** Build a flat snapshot of the current entries (server or client view). Out is reset first. */
	void BuildScopedEntries(TArray<FWorldHub_ScopedRepEntry>& Out) const;

	/**
	 * Called by the per-entry fast-array callbacks on clients after an add/change/remove. Pushes the
	 * affected entry (or, for the full set, all entries) into the local hub via SyncReplicatedState.
	 */
	void HandleReplicatedChange(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FSeam_NetValue& Value, bool bRemoved);

protected:
	/** Replicated world-state entries. */
	UPROPERTY(Replicated)
	FWorldHub_RepStateArray RepArray;

	/** True on the network authority (server / standalone). Gate every mutator on this. */
	bool HasAuthorityToMutate() const;

	/** Find the index of (Scope, Key)'s entry, or INDEX_NONE. */
	int32 IndexOf(const FWorldHub_Scope& Scope, const FGameplayTag& Key) const;

	/** Resolve the local world-hub subsystem (may be null very early). */
	UWorldHub_StateHubSubsystem* ResolveHub() const;
};
