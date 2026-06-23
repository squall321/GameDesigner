// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "SimAg_MemoryArray.generated.h"

class USimAg_MemoryComponent;
struct FSimAg_MemoryArray;

/**
 * One decaying remembered fact: "I saw a <Subject> at <WorldLocation> (entity <Entity>) on day
 * <LastSeenDays>, and I'm <Confidence> sure". Subject is a tag under SimAgNativeTags::Memory so the same
 * machinery remembers a resource pile, a threat, or another agent.
 *
 * This is a FFastArraySerializerItem so individual facts delta-replicate (refreshing one memory doesn't
 * resend the whole knowledge store), mirroring FSimAg_NeedMeter exactly. All members are plain replicable
 * types — NO FInstancedStruct here (the net rules forbid plain-replicated instanced structs).
 *
 * DECAY is computed, not stored: GetDecayedConfidence ages the stored Confidence by how long ago the
 * fact was last seen, using an exponential half-life in SIM DAYS (so a fast/slow day cycle scales it).
 * The component prunes facts whose decayed confidence falls below a settings threshold.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_MemoryFact : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** What kind of thing this fact is about (child of SimAg.Memory, e.g. SimAg.Memory.Resource.Wood). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Memory", meta = (Categories = "SimAg.Memory"))
	FGameplayTag Subject;

	/** Last-known world location of the subject. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Memory")
	FVector WorldLocation = FVector::ZeroVector;

	/** Stable id of the remembered entity, when the fact is about a specific entity (else invalid). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Memory")
	FSeam_EntityId Entity;

	/** Confidence in [0,1] at LastSeenDays (1 = just witnessed). Decays from here with elapsed time. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Memory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Confidence = 1.f;

	/**
	 * The simulation time, in fractional DAYS, at which this fact was last refreshed. Decay is measured
	 * from here against the live clock day so memory ages on simulation time, not real time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Memory")
	double LastSeenDays = 0.0;

	FSimAg_MemoryFact() = default;

	/**
	 * Confidence aged to NowDays by an exponential half-life (HalfLifeDays). Returns the stored
	 * Confidence when NowDays <= LastSeenDays (no negative elapsed) and clamps HalfLifeDays to a tiny
	 * positive minimum so a mis-authored zero half-life never divides by zero.
	 */
	float GetDecayedConfidence(double NowDays, float HalfLifeDays) const
	{
		const double Elapsed = NowDays - LastSeenDays;
		if (Elapsed <= 0.0)
		{
			return FMath::Clamp(Confidence, 0.f, 1.f);
		}
		const double SafeHalfLife = FMath::Max(static_cast<double>(HalfLifeDays), 1e-4);
		// 0.5 ^ (elapsed / halflife): exponential decay with the given half-life.
		const double Factor = FMath::Pow(0.5, Elapsed / SafeHalfLife);
		return FMath::Clamp(static_cast<float>(Confidence * Factor), 0.f, 1.f);
	}

	//~ FFastArraySerializerItem replication callbacks (clients only).
	void PostReplicatedAdd(const FSimAg_MemoryArray& InArraySerializer);
	void PostReplicatedChange(const FSimAg_MemoryArray& InArraySerializer);
	void PreReplicatedRemove(const FSimAg_MemoryArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the agent's remembered facts. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize so only changed facts cross the wire on each replication flush. Exact mirror of
 * FSimAg_NeedsArray.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_MemoryArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated facts. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Memory")
	TArray<FSimAg_MemoryFact> Facts;

	/** Non-replicated back-pointer to the owning component, for change notifications on clients. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<USimAg_MemoryComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimAg_MemoryFact, FSimAg_MemoryArray>(Facts, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the memory array. */
template<>
struct TStructOpsTypeTraits<FSimAg_MemoryArray> : public TStructOpsTypeTraitsBase2<FSimAg_MemoryArray>
{
	enum { WithNetDeltaSerializer = true };
};
