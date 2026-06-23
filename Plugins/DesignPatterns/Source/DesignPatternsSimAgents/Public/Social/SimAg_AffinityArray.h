// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Identity/Seam_EntityId.h"
#include "SimAg_AffinityArray.generated.h"

class USimAg_SocialComponent;
struct FSimAg_AffinityArray;

/**
 * One directed affinity edge: how this agent feels about another entity (Other), as a signed value in
 * [-1,1] (-1 enmity, 0 neutral, +1 friendship). Fast-array item so a single relationship delta-replicates
 * rather than resending the whole social graph. Plain replicable members only.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_Affinity : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** The other entity this edge is about. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Social")
	FSeam_EntityId Other;

	/** Signed feeling in [-1,1]. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Social", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float Value = 0.f;

	FSimAg_Affinity() = default;
	FSimAg_Affinity(const FSeam_EntityId& InOther, float InValue) : Other(InOther), Value(InValue) {}

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const FSimAg_AffinityArray& InArraySerializer);
	void PostReplicatedChange(const FSimAg_AffinityArray& InArraySerializer);
	void PreReplicatedRemove(const FSimAg_AffinityArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the agent's affinity edges. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize. Mirror of FSimAg_NeedsArray.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_AffinityArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated affinity edges. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Social")
	TArray<FSimAg_Affinity> Edges;

	/** Non-replicated back-pointer to the owning component, for change notifications on clients. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimAg_SocialComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimAg_Affinity, FSimAg_AffinityArray>(Edges, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the affinity array. */
template<>
struct TStructOpsTypeTraits<FSimAg_AffinityArray> : public TStructOpsTypeTraitsBase2<FSimAg_AffinityArray>
{
	enum { WithNetDeltaSerializer = true };
};
