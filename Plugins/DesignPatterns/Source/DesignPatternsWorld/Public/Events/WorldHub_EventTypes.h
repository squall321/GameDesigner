// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"
#include "Net/Seam_NetValue.h"
#include "Identity/Seam_EntityId.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "WorldHub_EventTypes.generated.h"

/**
 * The kind of hub mutation an event records. Stored as a tag-friendly enum so the event log can
 * classify changes (a set/clear/increment) without re-deriving them from values.
 */
UENUM(BlueprintType)
enum class EWorldHub_ChangeKind : uint8
{
	/** A value was set or replaced (the common case). */
	Set,

	/** A counter was incremented/decremented (NetValue carries the new total). */
	Increment,

	/** A value was cleared / removed (NetValue and LocalValue are unset). */
	Clear
};

/**
 * One append-only mutation event in the world-hub event log (EVENT SOURCING).
 *
 * Net-friendly values are stored as FSeam_NetValue (the only wire-safe variant); the lossless
 * LocalValue FInstancedStruct is LOCAL / SAVE only and is NEVER replicated directly. Timestamps come
 * from the deterministic, pause-aware ISeam_SimClock so a replay reproduces the same ordering.
 *
 * The struct holds NO UObject / weak references, so it round-trips cleanly through saves and can be
 * flattened into a bus payload or an FInstancedStruct at the ISeam_HubHistory boundary.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_HubEvent
{
	GENERATED_BODY()

	/** Monotonic sequence number (strictly increasing, never reused). 0 is the "before first event" sentinel. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Event")
	int64 Sequence = 0;

	/** Simulation seconds at the time of the mutation (deterministic, pause-aware). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Event")
	double SimTime = 0.0;

	/** Calendar day index at the time of the mutation. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Event")
	int32 DayNumber = 0;

	/** The scope of the mutated slot. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Event")
	FWorldHub_Scope Scope;

	/** The key of the mutated slot. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Event")
	FGameplayTag Key;

	/** How the value changed (set / increment / clear). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Event")
	EWorldHub_ChangeKind ChangeKind = EWorldHub_ChangeKind::Set;

	/** The net-friendly new value (unset for Clear or non-net Struct kinds). Wire-safe. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Event")
	FSeam_NetValue NetValue;

	/** The lossless new value for replay/save. LOCAL/SAVE only — NEVER replicated directly. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Event")
	FInstancedStruct LocalValue;

	/** Optional instigator entity ("who" caused the mutation), net-/save-stable. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|Event")
	FSeam_EntityId Instigator;

	FWorldHub_HubEvent() = default;
};

/**
 * Flat, FInstancedStruct-free boundary form of a hub event used at the ISeam_HubHistory seam and on
 * the changed feed. Drops the lossless LocalValue (keeping only the wire-safe NetValue), so a tooling
 * consumer never receives an arbitrary FInstancedStruct payload it cannot reason about.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_FlatEvent
{
	GENERATED_BODY()

	/** Monotonic sequence number. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Event")
	int64 Sequence = 0;

	/** Simulation seconds at the mutation. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Event")
	double SimTime = 0.0;

	/** The scope of the mutated slot. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Event")
	FWorldHub_Scope Scope;

	/** The key of the mutated slot. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Event")
	FGameplayTag Key;

	/** How the value changed. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Event")
	EWorldHub_ChangeKind ChangeKind = EWorldHub_ChangeKind::Set;

	/** The net-friendly new value. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Event")
	FSeam_NetValue NetValue;

	FWorldHub_FlatEvent() = default;

	/** Build a flat event from a full event (drops the lossless local payload). */
	explicit FWorldHub_FlatEvent(const FWorldHub_HubEvent& Event)
		: Sequence(Event.Sequence)
		, SimTime(Event.SimTime)
		, Scope(Event.Scope)
		, Key(Event.Key)
		, ChangeKind(Event.ChangeKind)
		, NetValue(Event.NetValue)
	{
	}
};
