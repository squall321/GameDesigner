// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Jobs/SimAg_JobTypes.h"
#include "SimAg_JobArray.generated.h"

class ASimAg_JobBoardReplicator;

/**
 * One replicated job posting. Wrapped as a fast-array item so individual posts / claims / completions
 * delta-replicate instead of resending the whole board. All members are plain replicable types
 * (FGuid / FGameplayTag / FVector / enum / FSeam_EntityId) — NO plain FInstancedStruct here, which the
 * net rules forbid; arbitrary per-job state, if ever needed, would ride inside this item exactly as the
 * grid module does with its cell payload.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_JobEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Unique posting id assigned by the board at PostJob time. Stable across its lifetime. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FGuid JobId;

	/** Kind of work (relevance/qualification matching keys off this). */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FGameplayTag JobKind;

	/** Capability/skill required to claim, or empty for "anyone". */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FGameplayTag RequiredSkill;

	/** Where the work happens, world space. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FVector WorldLocation = FVector::ZeroVector;

	/** Designer-authored relative importance (>= 0). */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	float Priority = 1.f;

	/** Stable id of the poster (building/faction/player). May be invalid. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FSeam_EntityId Poster;

	/** Stable id of the agent currently holding the job (invalid while Open). */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	FSeam_EntityId Claimant;

	/** Current lifecycle phase. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	ESimAg_JobState State = ESimAg_JobState::Open;

	FSimAg_JobEntry() = default;

	/** Build a fresh Open entry from a request, assigning the supplied id. */
	FSimAg_JobEntry(const FGuid& InId, const FSimAg_JobRequest& Request)
		: JobId(InId)
		, JobKind(Request.JobKind)
		, RequiredSkill(Request.RequiredSkill)
		, WorldLocation(Request.WorldLocation)
		, Priority(Request.Priority)
		, Poster(Request.Poster)
		, State(ESimAg_JobState::Open)
	{
	}

	/** Project this replicated entry into a copyable handle for brains/blackboards. */
	FSimAg_JobHandle ToHandle() const
	{
		FSimAg_JobHandle Handle;
		Handle.JobId = JobId;
		Handle.JobKind = JobKind;
		Handle.WorldLocation = WorldLocation;
		Handle.State = State;
		return Handle;
	}

	//~ FFastArraySerializerItem replication callbacks (invoked on clients only).
	void PreReplicatedRemove(const struct FSimAg_JobArray& InArraySerializer);
	void PostReplicatedAdd(const struct FSimAg_JobArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimAg_JobArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the board's postings. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize so only changed postings cross the wire. The owning-carrier back-pointer is
 * non-replicated and set on both server and client so per-item callbacks can notify it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_JobArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated job postings. */
	UPROPERTY(BlueprintReadOnly, Category = "SimAgents|Jobs")
	TArray<FSimAg_JobEntry> Entries;

	/** Non-replicated back-pointer to the owning carrier, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ASimAg_JobBoardReplicator> OwnerCarrier = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimAg_JobEntry, FSimAg_JobArray>(Entries, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the job array. */
template<>
struct TStructOpsTypeTraits<FSimAg_JobArray> : public TStructOpsTypeTraitsBase2<FSimAg_JobArray>
{
	enum { WithNetDeltaSerializer = true };
};
