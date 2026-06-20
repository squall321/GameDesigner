// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Jobs/SimAg_JobArray.h"
#include "Jobs/SimAg_JobTypes.h"
#include "SimAg_JobBoardReplicator.generated.h"

/**
 * Fired (server and clients) whenever a single posting changes on this carrier — after replication on
 * clients. Carries the affected posting id so listeners can refresh just that row.
 * @param Carrier The board carrier whose posting changed.
 * @param JobId   The affected posting.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimAg_OnJobChanged,
	ASimAg_JobBoardReplicator*, Carrier, FGuid, JobId);

/**
 * Replicated authority carrier for the world's job board.
 *
 * SimAgents subsystems are never replicated; all authoritative job state lives on this AInfo. The job
 * board world subsystem spawns exactly one carrier on the server and routes every mutation through this
 * actor's authority-guarded mutators. Clients receive posting deltas via the FFastArraySerializer and
 * observe changes through OnJobChanged.
 *
 * Net dormancy: the carrier sits DORMANT_Initial and only flushes dormancy when a posting actually
 * changes, so an idle board costs no per-frame replication bandwidth.
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API ASimAg_JobBoardReplicator : public AInfo
{
	GENERATED_BODY()

public:
	ASimAg_JobBoardReplicator();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	//~ End AActor

	// --- Authority mutators (AUTHORITY ONLY; each early-returns on clients) ---

	/**
	 * Append a new Open posting built from Request, assigning a fresh id. AUTHORITY ONLY.
	 * @return the new posting id (invalid on clients or invalid request).
	 */
	FGuid AddJob(const FSimAg_JobRequest& Request);

	/**
	 * Transition an Open posting to Claimed by Claimant. AUTHORITY ONLY. Fails if the posting is missing
	 * or not Open. @return true if the claim succeeded.
	 */
	bool ClaimJob(const FGuid& JobId, const FSeam_EntityId& Claimant);

	/**
	 * Transition a posting to Completed. AUTHORITY ONLY. @return true if a posting existed and changed.
	 */
	bool CompleteJob(const FGuid& JobId);

	/**
	 * Transition a posting to Cancelled (timed out / abandoned). AUTHORITY ONLY.
	 * @return true if a posting existed and changed.
	 */
	bool CancelJob(const FGuid& JobId);

	/**
	 * Remove all postings in a terminal state (Completed / Cancelled). AUTHORITY ONLY.
	 * @return number of postings pruned.
	 */
	int32 PruneTerminal();

	// --- Reads (safe on clients; observe replicated state) ---

	/** Find the posting for JobId, or null. Const, client-safe. */
	const FSimAg_JobEntry* FindEntry(const FGuid& JobId) const;

	/** Read-only access to all postings on this carrier. */
	const TArray<FSimAg_JobEntry>& GetEntries() const { return Jobs.Entries; }

	/** Number of postings currently in the Open state. Client-safe. */
	int32 CountOpen() const;

	/** Fired when any posting changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Jobs")
	FSimAg_OnJobChanged OnJobChanged;

	/** Called by the fast-array item callbacks on clients to surface a replicated posting change. */
	void HandleReplicatedJobChange(const FGuid& JobId);

private:
	/** Replicated postings for this board (delta-serialized). */
	UPROPERTY(Replicated)
	FSimAg_JobArray Jobs;

	/** Mutable find for the authority mutators; returns null if absent. */
	FSimAg_JobEntry* FindEntryMutable(const FGuid& JobId);

	/** Wake the actor from net dormancy so a just-changed delta replicates this frame. */
	void WakeForChange();
};
