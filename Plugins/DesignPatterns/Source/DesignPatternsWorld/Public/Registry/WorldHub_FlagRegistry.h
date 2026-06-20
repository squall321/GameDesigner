// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Hub/WorldHub_Scope.h"
#include "Registry/WorldHub_FlagTypes.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "WorldHub_FlagRegistry.generated.h"

class UWorldHub_FlagSetDataAsset;

/**
 * One replicable (Key, Scope, Value) triple produced by the registry for the net carrier.
 *
 * The types-area FWorldHub_RepStateEntry deliberately carries only (Key, Value) and documents that
 * the scope is carried by the enclosing fast-array item. This struct is that enclosing shape on the
 * registry side: a flat snapshot row that pairs a replicable flag's scope with its net projection,
 * so the carrier and SyncReplicatedState path always know which scope a value belongs to.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_ScopedRepEntry
{
	GENERATED_BODY()

	/** The scope the value belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	FWorldHub_Scope Scope;

	/** The flag key. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	FGameplayTag Key;

	/** The net-friendly projection of the flag's value. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	FSeam_NetValue Value;

	FWorldHub_ScopedRepEntry() = default;
	FWorldHub_ScopedRepEntry(const FWorldHub_Scope& InScope, const FGameplayTag& InKey, const FSeam_NetValue& InValue)
		: Scope(InScope), Key(InKey), Value(InValue) {}
};

/**
 * The composite (Scope, Key) address of one stored value slot.
 *
 * A reflected USTRUCT so it can be a UPROPERTY TMap key — which is what keeps the slot map's
 * FInstancedStruct values (and any UObject references they contain) GC-visible. Equality and hashing
 * delegate to the scope (which already hashes only its active fields) and the key tag.
 */
USTRUCT()
struct DESIGNPATTERNSWORLD_API FWorldHub_SlotAddress
{
	GENERATED_BODY()

	/** The scope this slot belongs to. */
	UPROPERTY()
	FWorldHub_Scope Scope;

	/** The flag key. */
	UPROPERTY()
	FGameplayTag Key;

	FWorldHub_SlotAddress() = default;
	FWorldHub_SlotAddress(const FWorldHub_Scope& InScope, const FGameplayTag& InKey) : Scope(InScope), Key(InKey) {}

	bool operator==(const FWorldHub_SlotAddress& Other) const { return Scope == Other.Scope && Key == Other.Key; }
	friend uint32 GetTypeHash(const FWorldHub_SlotAddress& A) { return HashCombine(GetTypeHash(A.Scope), GetTypeHash(A.Key)); }
};

/**
 * The in-memory store of world-hub flag/variable/counter values, addressed by (Key, Scope).
 *
 * A plain UObject (NOT a subsystem, NEVER replicated) owned by the state-hub subsystem as an
 * instanced subobject. It is the authoritative source of truth on the server and the replicated
 * mirror on clients. It also holds a definition index (Key -> FWorldHub_FlagDefinition) loaded from
 * flag-set data assets, used to type-check writes, clamp counters, seed defaults and decide
 * replicate/save policy.
 *
 * The registry performs NO authority checks itself — it is a passive container. The owning hub
 * subsystem guards authority at the TOP of each mutator before calling in here.
 */
UCLASS()
class DESIGNPATTERNSWORLD_API UWorldHub_FlagRegistry : public UObject
{
	GENERATED_BODY()

public:
	// ---- Definitions --------------------------------------------------------------------------

	/**
	 * Seed default values and register definitions from a flag-set asset.
	 *
	 * For each definition: the definition is indexed by Key, and (unless a value already exists for
	 * (Key, Global) and bOverwriteExisting is false) the Global-scope slot is seeded with the
	 * definition's default (or the type's natural zero when no default is authored).
	 *
	 * Null asset is a safe no-op. @return number of definitions applied.
	 */
	int32 LoadDefaultsFrom(const UWorldHub_FlagSetDataAsset* FlagSet, bool bOverwriteExisting = false);

	/** Find the definition for Key, or null if none was loaded. */
	const FWorldHub_FlagDefinition* FindDefinition(const FGameplayTag& Key) const;

	// ---- Typed convenience writes/reads (Global scope unless specified) ------------------------

	/**
	 * Set a boolean flag. Creates the slot if absent. @return true if the stored value changed.
	 * Honors any definition's replicate/save policy; with no definition, defaults to replicate=false.
	 */
	bool SetFlag(const FGameplayTag& Key, bool bValue, const FWorldHub_Scope& Scope = FWorldHub_Scope::Global());

	/** Read Key/Scope as a boolean; returns bDefault if absent or not boolean. */
	bool GetFlag(const FGameplayTag& Key, bool bDefault = false, const FWorldHub_Scope& Scope = FWorldHub_Scope::Global()) const;

	/**
	 * Add Delta to a counter slot, clamping the result to the definition's [CounterMin, CounterMax]
	 * (unbounded when no definition exists). Creates the slot at the clamped Delta if absent.
	 * @return the new (clamped) counter value.
	 */
	int64 IncrementCounter(const FGameplayTag& Key, int64 Delta = 1, const FWorldHub_Scope& Scope = FWorldHub_Scope::Global());

	/** Read Key/Scope as an integer counter; returns Default if absent or non-integer. */
	int64 GetCounter(const FGameplayTag& Key, int64 Default = 0, const FWorldHub_Scope& Scope = FWorldHub_Scope::Global()) const;

	/**
	 * Set an arbitrary variable value (any FInstancedStruct). Net-friendly inner types still
	 * replicate if the definition says so; non-net inner types are local/save only.
	 * @return true if the stored value changed.
	 */
	bool SetVariable(const FGameplayTag& Key, const FInstancedStruct& Value, const FWorldHub_Scope& Scope = FWorldHub_Scope::Global());

	/** Read Key/Scope's raw value into Out. @return true if a value exists. */
	bool GetVariable(const FGameplayTag& Key, FInstancedStruct& Out, const FWorldHub_Scope& Scope = FWorldHub_Scope::Global()) const;

	// ---- Generic slot access ------------------------------------------------------------------

	/** Read the full value record for (Key, Scope). @return true (and fills Out) if present. */
	bool GetValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope, FWorldHub_FlagValue& Out) const;

	/**
	 * Set the full value record for (Key, Scope). Counter inner ints are clamped to the definition.
	 * @return true if the slot was created or its value changed.
	 */
	bool SetValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope, const FWorldHub_FlagValue& Value);

	/** True if a concrete (stored) value exists for the EXACT (Key, Scope) (no fallback). */
	bool HasValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope) const;

	/** Remove the slot for (Key, Scope). @return true if a slot existed and was removed. */
	bool ClearValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope);

	/** Drop every stored slot (definitions are retained). */
	void ResetValues();

	/** Drop every stored slot AND every definition. */
	void ResetAll();

	/** Number of stored value slots. */
	int32 NumValues() const { return Slots.Num(); }

	// ---- Save / replication bridges -----------------------------------------------------------

	/** One stored slot as a flat (Scope, Key, Value) tuple, for save capture and bulk transfer. */
	struct FSlotRecord
	{
		FWorldHub_Scope Scope;
		FGameplayTag Key;
		FWorldHub_FlagValue Value;
	};

	/** Append every slot whose definition opts into save (bSave) into Out. Out is appended, not cleared. */
	void CaptureSaveSlots(TArray<FSlotRecord>& Out) const;

	/** Replace all stored slots from a flat list of records (used by save restore). */
	void RestoreSaveSlots(const TArray<FSlotRecord>& Records);

	/**
	 * Append every REPLICABLE slot (definition opts into replicate AND the inner value projects to a
	 * net-friendly FSeam_NetValue) into Out as flat (Scope, Key, NetValue) rows. Out is appended.
	 */
	void GetReplicatedEntries(TArray<FWorldHub_ScopedRepEntry>& Out) const;

	/**
	 * Apply a single replicated (Scope, Key, NetValue) row locally (CLIENT mirror path). Converts the
	 * net value back into an FInstancedStruct slot, marking it replicated. @return true if changed.
	 */
	bool ApplyReplicatedEntry(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FSeam_NetValue& NetValue);

	/** Remove all replicated (non-save-only) slots not present in KeepKeys, used during a full sync. */
	void PruneReplicatedSlotsNotIn(const TSet<TPair<FWorldHub_Scope, FGameplayTag>>& KeepKeys);

	/** One-line summary for the hub debug string. */
	FString ToDebugString() const;

private:
	/** Convenience alias for the composite slot key. */
	using FSlotKey = FWorldHub_SlotAddress;

	/**
	 * (Scope, Key) -> value. The single source of truth. A UPROPERTY so the FInstancedStruct values
	 * and any inner UObject references are GC-visible; it is NOT a replicated member (the registry is
	 * never itself replicated — net state flows through the rep-component carrier).
	 */
	UPROPERTY()
	TMap<FWorldHub_SlotAddress, FWorldHub_FlagValue> Slots;

	/** Key -> definition, loaded from flag-set assets. Drives typing, clamping, defaults and policy. */
	UPROPERTY()
	TMap<FGameplayTag, FWorldHub_FlagDefinition> Definitions;

	/** Build the type's natural-zero value as an FInstancedStruct for SeedType. */
	static void MakeZeroValue(EWorldHub_FlagValueType ValueType, FInstancedStruct& Out);

	/** Clamp a slot's value in place if its definition is a counter. */
	void ClampIfCounter(const FGameplayTag& Key, FWorldHub_FlagValue& Value) const;

	/** Resolve whether a slot replicates: definition policy if known, else the slot's own bReplicate. */
	bool SlotReplicates(const FGameplayTag& Key, const FWorldHub_FlagValue& Value) const;

	/** Resolve whether a slot saves: definition policy if known, else the slot's own bSave. */
	bool SlotSaves(const FGameplayTag& Key, const FWorldHub_FlagValue& Value) const;
};
