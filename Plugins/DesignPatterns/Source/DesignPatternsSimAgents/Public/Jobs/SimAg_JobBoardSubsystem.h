// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Persist/Seam_Persistable.h"
#include "Jobs/SimAg_JobProvider.h"
#include "Jobs/SimAg_JobTypes.h"
#include "SimAg_JobBoardSubsystem.generated.h"

class ASimAg_JobBoardReplicator;

/**
 * World-scoped job board: the authority-side router that owns the single replicated postings carrier
 * and the read/claim API agents use through the ISimAg_JobProvider seam.
 *
 * RESPONSIBILITIES
 *  - Implements ISimAg_JobProvider (post / query / claim / complete). Registers itself under
 *    SimAgNativeTags::Service_JobBoard (WeakObserved) so the agent brain reaches it by stable tag
 *    without depending on this concrete type.
 *  - Implements ISeam_Persistable so a UDP_SaveGame can capture/restore the board through the universal
 *    save seam — capture/restore are AUTHORITY ONLY.
 *  - Holds NO replicated state itself (subsystems are never replicated). On authority it lazily spawns
 *    ONE ASimAg_JobBoardReplicator and routes every mutation to that carrier, whose authority-guarded
 *    mutators are the real source of truth. Clients read the replicated postings off the carrier.
 *  - Emits FSimAg_JobEvent on the message bus when postings change, so UI/other systems need not poll.
 *
 * QueryBestJobFor is side-effect-free and client-safe; ClaimJob/CompleteJob/PostJob early-return on
 * clients. Client→server claim intent must arrive through a player-owned component, never a direct
 * client call here.
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API USimAg_JobBoardSubsystem
	: public UDP_WorldSubsystem
	, public ISimAg_JobProvider
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority; declare our own. True on the server / standalone
	 * (anything that is not a pure net client), gating all carrier spawning and mutation.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	//~ Begin ISimAg_JobProvider
	virtual FGuid PostJob_Implementation(const FSimAg_JobRequest& Request) override;
	virtual FSimAg_JobHandle ClaimJob_Implementation(FGameplayTag JobKind, const FVector& AgentLocation) override;
	virtual void CompleteJob_Implementation(const FGuid& JobId) override;
	virtual FSimAg_JobHandle QueryBestJobFor_Implementation(FGameplayTag JobKind, const FVector& AgentLocation) const override;
	//~ End ISimAg_JobProvider

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	/**
	 * Claim the best open posting of JobKind near AgentLocation FOR a specific agent identity. This is
	 * the server-side authority API the player-owned component routes validated intent into (the seam's
	 * ClaimJob claims for an anonymous/derived claimant; this overload records who claimed it). AUTHORITY
	 * ONLY. @return a handle to the claimed posting, or an invalid handle.
	 */
	FSimAg_JobHandle ClaimJobForAgent(FGameplayTag JobKind, const FVector& AgentLocation, const FSeam_EntityId& AgentId);

	/** Cancel a posting (timed out / abandoned). AUTHORITY ONLY. @return true if it changed. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Jobs")
	bool CancelJob(const FGuid& JobId);

	/** Drop all terminal (completed/cancelled) postings. AUTHORITY ONLY. @return count pruned. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Jobs")
	int32 PruneCompletedJobs();

	/** Number of currently-open postings on the board (client-safe read). */
	UFUNCTION(BlueprintPure, Category = "SimAgents|Jobs")
	int32 GetOpenJobCount() const;

	/** The live carrier, spawning it on authority if it does not yet exist. Null on clients with none. */
	ASimAg_JobBoardReplicator* GetOrSpawnBoard(bool bSpawnIfMissing);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/**
	 * The single board carrier. Owned by the world (a runtime, transient actor); held WEAK (non-owning),
	 * always null-checked before deref. NON-replicated — the subsystem never crosses the wire.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ASimAg_JobBoardReplicator> BoardCarrier;

	/** Service-locator key we registered under, for clean unregister on teardown. */
	UPROPERTY(Transient)
	FGameplayTag RegisteredServiceTag;

	/** Max postings the relevance scan considers (cached from settings). */
	int32 RelevancyCap = 16;

	/** Discover an already-replicated carrier on clients, or cache the authority one. */
	ASimAg_JobBoardReplicator* ResolveBoard() const;

	/** Register this subsystem under the job-board service tag (WeakObserved). */
	void RegisterAsJobProvider();

	/** Broadcast a FSimAg_JobEvent for a posting through the message bus. */
	void EmitJobEvent(const FSimAg_JobEntry& Entry) const;

	/**
	 * Core relevance scan shared by query and claim: pick the best Open posting of JobKind near
	 * AgentLocation, capping the candidate set at RelevancyCap. Returns null when none qualify.
	 */
	const FSimAg_JobEntry* FindBestOpen(const ASimAg_JobBoardReplicator* Board, const FGameplayTag& JobKind, const FVector& AgentLocation) const;
};
