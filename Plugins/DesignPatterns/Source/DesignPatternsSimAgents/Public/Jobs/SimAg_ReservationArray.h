// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Identity/Seam_EntityId.h"
#include "SimAg_ReservationArray.generated.h"

class ASimAg_ReservationReplicator;
struct FSimAg_ReservationArray;

/**
 * One active reservation: Agent has claimed Target until ExpiryDays (simulation fractional days). Past
 * the expiry the router treats it as free, so a dead/stuck agent never permanently locks a resource.
 *
 * Fast-array item so a single reserve/release delta-replicates. Plain replicable members only.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_Reservation : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The reserved target entity (stockpile, workstation, node...). */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FSeam_EntityId Target;

	/** The agent holding the reservation. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FSeam_EntityId Agent;

	/** Simulation time (fractional DAYS) at/after which this reservation auto-expires. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	double ExpiryDays = 0.0;

	FSimAg_Reservation() = default;
	FSimAg_Reservation(const FSeam_EntityId& InTarget, const FSeam_EntityId& InAgent, double InExpiryDays)
		: Target(InTarget), Agent(InAgent), ExpiryDays(InExpiryDays) {}

	/** True if this reservation is still valid at NowDays. */
	bool IsLiveAt(double NowDays) const { return NowDays < ExpiryDays; }

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const FSimAg_ReservationArray& InArraySerializer);
	void PostReplicatedChange(const FSimAg_ReservationArray& InArraySerializer);
	void PreReplicatedRemove(const FSimAg_ReservationArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the world's active reservations. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize. Mirror of FSimAg_JobArray.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_ReservationArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated reservations. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	TArray<FSimAg_Reservation> Reservations;

	/** Non-replicated back-pointer to the owning carrier, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ASimAg_ReservationReplicator> OwnerCarrier = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimAg_Reservation, FSimAg_ReservationArray>(Reservations, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the reservation array. */
template<>
struct TStructOpsTypeTraits<FSimAg_ReservationArray> : public TStructOpsTypeTraitsBase2<FSimAg_ReservationArray>
{
	enum { WithNetDeltaSerializer = true };
};
