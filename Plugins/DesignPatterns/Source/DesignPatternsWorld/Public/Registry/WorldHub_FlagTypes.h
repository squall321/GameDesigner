// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "WorldHub_FlagTypes.generated.h"

/**
 * The kind of value a world-hub flag holds.
 *
 * Replicable kinds map onto ESeam_NetValueType (the only variant allowed across the wire). The
 * Counter kind is a bounded integer that exposes increment/decrement semantics on top of the Int
 * representation. Struct is a save-/server-local-only arbitrary FInstancedStruct value and is NEVER
 * replicated directly (it has no FSeam_NetValue projection).
 */
UENUM(BlueprintType)
enum class EWorldHub_FlagValueType : uint8
{
	/** A boolean flag (replicable). */
	Bool,

	/** A 64-bit integer variable (replicable). */
	Int,

	/** A double-precision float variable (replicable). */
	Float,

	/** A 3D vector (replicable). */
	Vector,

	/** A gameplay tag value (replicable). */
	Tag,

	/** An FName value (replicable). */
	Name,

	/** A bounded integer with increment/decrement semantics (replicable via its Int projection). */
	Counter,

	/** An arbitrary local/save-only struct value (FInstancedStruct). NEVER replicated directly. */
	Struct
};

/** @return the FSeam_NetValue discriminator a replicable flag kind projects onto. */
DESIGNPATTERNSWORLD_API ESeam_NetValueType WorldHub_FlagTypeToNetValueType(EWorldHub_FlagValueType FlagType);

/** @return true if this flag kind can be projected onto an FSeam_NetValue and thus replicated. */
DESIGNPATTERNSWORLD_API bool WorldHub_IsReplicableFlagType(EWorldHub_FlagValueType FlagType);

/**
 * The runtime value of a single world-hub flag slot.
 *
 * The authoritative/local/save value is always held as an FInstancedStruct (the lossless,
 * arbitrary-type representation used on the server and in saves). When the flag is replicated,
 * the hub additionally maintains an FSeam_NetValue projection inside a fast-array item — this
 * struct is the LOCAL side and intentionally does not itself replicate the FInstancedStruct.
 *
 * bReplicate/bSave mirror the owning definition for fast per-slot decisions without a definition
 * lookup on the hot path.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_FlagValue
{
	GENERATED_BODY()

	/**
	 * The lossless local/save value. For replicable kinds this is kept in sync with the net
	 * projection by the hub. Never published directly to the network (an FInstancedStruct is not a
	 * valid plain replicated UPROPERTY — only its FSeam_NetValue projection crosses the wire).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Flag")
	FInstancedStruct Value;

	/** Whether this slot's value is mirrored to clients via the hub's replicated carrier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Flag")
	bool bReplicate = false;

	/** Whether this slot's value is captured into the save game. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Flag")
	bool bSave = true;

	FWorldHub_FlagValue() = default;

	/** @return true if a concrete value is currently stored. */
	bool IsSet() const { return Value.IsValid(); }

	/** Clear the stored value (leaves the replicate/save policy flags untouched). */
	void ClearValue() { Value.Reset(); }
};

/**
 * The design-time definition of a single world-hub flag/variable/counter.
 *
 * Authored inside a UWorldHub_FlagSetDataAsset. The hub uses these definitions to:
 *  - validate writes (type must match ValueType),
 *  - seed default values when a slot is first read/initialized,
 *  - decide replication/save policy,
 *  - clamp Counter kinds to [CounterMin, CounterMax].
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_FlagDefinition
{
	GENERATED_BODY()

	/** Stable identity of this flag (e.g. DP.WorldHub.Flag.TutorialDone). Must be unique in a set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	FGameplayTag Key;

	/** The value kind this flag holds. Writes of a mismatched kind are rejected by the hub. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	EWorldHub_FlagValueType ValueType = EWorldHub_FlagValueType::Bool;

	/**
	 * The default value used to seed a slot on first access. May be empty, in which case the hub
	 * seeds the type's natural zero (false / 0 / 0.0 / ZeroVector / empty tag / NAME_None / Min for
	 * a counter). For Struct kinds the inner type should match the intended payload.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	FInstancedStruct DefaultValue;

	/** Whether values of this flag are mirrored to clients. Ignored for non-replicable kinds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	bool bReplicate = false;

	/** Whether values of this flag are captured into the save game. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	bool bSave = true;

	/** Inclusive lower bound applied to Counter kinds (ignored for other kinds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "ValueType == EWorldHub_FlagValueType::Counter"), Category = "DesignPatterns|WorldHub|Flag")
	int64 CounterMin = 0;

	/** Inclusive upper bound applied to Counter kinds (ignored for other kinds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (EditCondition = "ValueType == EWorldHub_FlagValueType::Counter"), Category = "DesignPatterns|WorldHub|Flag")
	int64 CounterMax = TNumericLimits<int64>::Max();

	FWorldHub_FlagDefinition() = default;

	/** @return true if this definition is replicable (kind is replicable AND bReplicate is set). */
	bool ShouldReplicate() const { return bReplicate && WorldHub_IsReplicableFlagType(ValueType); }

	/** Clamp a raw counter value into [CounterMin, CounterMax]. */
	int64 ClampCounter(int64 Raw) const { return FMath::Clamp(Raw, CounterMin, CounterMax); }
};

/**
 * One replicated entry of world-hub state: a flag key paired with its net-friendly value.
 *
 * Intended to live inside a fast-array serializer item on the hub's replicated carrier component
 * (the engine delta-serializes the FSeam_NetValue compactly). The scope is carried alongside this
 * entry by the enclosing fast-array item, keeping this struct focused on (key, value).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_RepStateEntry
{
	GENERATED_BODY()

	/** The flag key this replicated value belongs to. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	FGameplayTag Key;

	/** The net-friendly value (the only arbitrary value allowed across the wire). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Flag")
	FSeam_NetValue Value;

	FWorldHub_RepStateEntry() = default;

	FWorldHub_RepStateEntry(const FGameplayTag& InKey, const FSeam_NetValue& InValue)
		: Key(InKey)
		, Value(InValue)
	{
	}

	bool operator==(const FWorldHub_RepStateEntry& Other) const
	{
		return Key == Other.Key && Value == Other.Value;
	}

	bool operator!=(const FWorldHub_RepStateEntry& Other) const { return !(*this == Other); }
};
