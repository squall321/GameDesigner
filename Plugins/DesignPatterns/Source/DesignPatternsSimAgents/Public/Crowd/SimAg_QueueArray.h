// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Identity/Seam_EntityId.h"
#include "SimAg_QueueArray.generated.h"

class ASimAg_QueueVolume;
struct FSimAg_QueueArray;

/**
 * One waiting agent in a queue, with its ordinal position. Fast-array item so a single enqueue/dequeue
 * delta-replicates. Plain replicable members only.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_QueueSlot : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The queued agent. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Crowd")
	FSeam_EntityId Agent;

	/** Monotonic order ticket (lower = earlier). Lets the queue preserve order across array churn. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Crowd")
	int64 Ticket = 0;

	FSimAg_QueueSlot() = default;
	FSimAg_QueueSlot(const FSeam_EntityId& InAgent, int64 InTicket) : Agent(InAgent), Ticket(InTicket) {}

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const FSimAg_QueueArray& InArraySerializer);
	void PostReplicatedChange(const FSimAg_QueueArray& InArraySerializer);
	void PreReplicatedRemove(const FSimAg_QueueArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the queue's ordered waiting agents. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize. Mirror of FSimAg_JobArray.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_QueueArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated queue slots (kept sorted by Ticket on the authority). */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Crowd")
	TArray<FSimAg_QueueSlot> Slots;

	/** Non-replicated back-pointer to the owning volume, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ASimAg_QueueVolume> OwnerVolume = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimAg_QueueSlot, FSimAg_QueueArray>(Slots, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the queue array. */
template<>
struct TStructOpsTypeTraits<FSimAg_QueueArray> : public TStructOpsTypeTraitsBase2<FSimAg_QueueArray>
{
	enum { WithNetDeltaSerializer = true };
};
