// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Jobs/SimAg_JobTypes.h"
#include "SimAg_JobProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USimAg_JobProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Job-board seam: post work, query the best available posting for an agent, claim and complete it.
 *
 * The agent brain (utility-AI job strategy) reaches the board ONLY through this interface — it never
 * depends on the concrete USimAg_JobBoardSubsystem — so a board can be supplied by the module's world
 * subsystem, a squad coordinator, or a quest script and swapped freely. Producers (buildings, faction
 * AI) call PostJob; the subsystem implementation owns the authoritative posting list and routes every
 * mutation through an authority-guarded replicated carrier.
 *
 * All claim/complete calls are authoritative concepts: the implementer guards authority internally and
 * returns an invalid handle / no-op on clients. Client→server intent must travel through a player-owned
 * component (see USimAg_AgentComponent), never by a client calling these directly.
 */
class DESIGNPATTERNSSIMAGENTS_API ISimAg_JobProvider
{
	GENERATED_BODY()

public:
	/**
	 * Post a new unit of work. AUTHORITY ONLY in the implementer; returns an invalid guid on clients.
	 * @return the assigned posting id (valid on success).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Jobs")
	FGuid PostJob(const FSimAg_JobRequest& Request);

	/**
	 * Atomically claim the best open posting of JobKind near AgentLocation for the calling agent.
	 * AUTHORITY ONLY in the implementer. The job transitions to Claimed and is no longer offered.
	 * @return a handle to the claimed posting, or an invalid handle if none was available.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Jobs")
	FSimAg_JobHandle ClaimJob(FGameplayTag JobKind, const FVector& AgentLocation);

	/**
	 * Mark a posting completed. AUTHORITY ONLY in the implementer; no-op on clients. The posting moves
	 * to Completed and becomes eligible for pruning.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Jobs")
	void CompleteJob(const FGuid& JobId);

	/**
	 * SIDE-EFFECT-FREE read: find the best open posting of JobKind near AgentLocation WITHOUT claiming
	 * it. The brain calls this every decision pass to score "should I take a job?"; only Execute then
	 * calls ClaimJob. Safe on clients (reads replicated state); returns an invalid handle when none.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|SimAgents|Jobs")
	FSimAg_JobHandle QueryBestJobFor(FGameplayTag JobKind, const FVector& AgentLocation) const;
};
