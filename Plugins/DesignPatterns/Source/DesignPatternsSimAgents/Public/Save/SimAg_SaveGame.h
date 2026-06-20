// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Save/DPSaveGame.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Jobs/SimAg_JobTypes.h"

// FInstancedStruct version-gated include. A full FInstancedStruct is fine in a SAVE object (it is
// serialized via UPROPERTY SaveGame, not replicated) — the no-plain-replicated rule is net-only.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// .generated.h MUST be the last include (UnrealHeaderTool requirement).
#include "SimAg_SaveGame.generated.h"

class UWorld;

/**
 * One persisted job posting. Save-side records mirror the replicated FSimAg_JobEntry but live in a
 * plain struct (saves are local-only; the net path uses the fast array instead).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_SavedJob
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FGuid JobId;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FGameplayTag JobKind;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FGameplayTag RequiredSkill;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FVector WorldLocation = FVector::ZeroVector;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	float Priority = 1.f;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FSeam_EntityId Poster;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FSeam_EntityId Claimant;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	ESimAg_JobState State = ESimAg_JobState::Open;
};

/**
 * The job board's complete CaptureState record: every posting at capture time. This is the concrete
 * type the board's ISeam_Persistable::CaptureState writes into the FInstancedStruct out-parameter, and
 * RestoreState reads back out of it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_JobBoardRecord
{
	GENERATED_BODY()

	/** Every posting on the board at capture time. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	TArray<FSimAg_SavedJob> Jobs;
};

/**
 * One agent's CaptureState record: its stable identity plus the brain state worth persisting (current
 * activity and last move target). The concrete type written by USimAg_AgentComponent::CaptureState.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_AgentRecord
{
	GENERATED_BODY()

	/** Stable id of the agent (matches the entity-identity seam on the owning actor). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FSeam_EntityId AgentId;

	/** The agent's archetype/agent tag, so a restore can validate the record routes to the right pawn. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FGameplayTag AgentTag;

	/** The replicated current activity at capture time. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FGameplayTag CurrentActivity;

	/** The brain's last chosen move target, so a reloaded agent resumes toward it. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FVector MoveTarget = FVector::ZeroVector;

	/** Whether MoveTarget was set/meaningful at capture time. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	bool bHasMoveTarget = false;
};

/**
 * Save object for the SimAgents world: the job board record plus a record per agent.
 *
 * OnPreSave (game thread) gathers every ISeam_Persistable participant whose persistence kind belongs to
 * this module — the job board subsystem and each agent component — by calling CaptureState, and stores
 * the resulting FInstancedStructs. OnPostLoad scatters them back via RestoreState, which the
 * participants themselves authority-guard. Gather/scatter happen ON THE GAME THREAD, as the save
 * subsystem's threading contract requires (only the resulting byte buffer is touched off-thread).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSSIMAGENTS_API USimAg_SaveGame : public UDP_SaveGame
{
	GENERATED_BODY()

public:
	/** The job board's captured postings. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FSimAg_JobBoardRecord JobBoard;

	/**
	 * One opaque capture record per gathered participant, paired with its persistence-kind tag so
	 * RestoreInto can route each record back to a participant advertising the same kind. Agent records
	 * and the job-board record both live here as FInstancedStructs.
	 */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	TArray<FInstancedStruct> ParticipantRecords;

	/** Persistence kinds parallel to ParticipantRecords (same index), for routing on restore. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	TArray<FGameplayTag> ParticipantKinds;

	/**
	 * Gather every SimAgents ISeam_Persistable participant in World on the GAME THREAD and snapshot it.
	 * Captures the job board subsystem and every USimAg_AgentComponent. AUTHORITY ONLY (returns false on
	 * a client, which lacks the authoritative state). Replaces this object's record arrays.
	 * @return true if capture ran on authority.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Save")
	bool CaptureFrom(UWorld* World);

	/**
	 * Scatter the captured records back into World's participants on the GAME THREAD by calling
	 * RestoreState (which each participant authority-guards). AUTHORITY ONLY (returns 0 on a client).
	 * @return number of records successfully routed to a participant.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Save")
	int32 RestoreInto(UWorld* World) const;

	//~ Begin UDP_SaveGame
	virtual void OnPreSave_Implementation() override;
	virtual void OnPostLoad_Implementation() override;
	//~ End UDP_SaveGame

private:
	/** True on the server / standalone for the given world (mirrors the subsystem authority rule). */
	static bool WorldHasAuthority(const UWorld* World);
};
