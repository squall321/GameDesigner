// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Identity/Seam_EntityId.h"
#include "Crowd/SimAg_QueueArray.h"
#include "SimAg_QueueVolume.generated.h"

/** Fired (server and clients) when the queue membership/order changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimAg_OnQueueChanged, ASimAg_QueueVolume*, QueueVolume);

/**
 * A replicated queuing point (a shop counter, a well, a gate). Agents request a slot and receive a world
 * stand-position spaced behind the head of the queue, so a crowd lines up instead of mobbing the point.
 *
 * All queue state lives on this AInfo (subsystems are never replicated). Authority-guarded Enqueue/Dequeue
 * keep the ordered fast array; clients observe and read their stand position. DORMANT_Initial until the
 * queue changes (mirrors ASimAg_JobBoardReplicator).
 *
 * The queue direction/anchor is the actor's transform: the head stands at the actor location and each
 * subsequent slot is QueueSlotSpacing further along -X (behind), oriented by the actor's rotation.
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API ASimAg_QueueVolume : public AInfo
{
	GENERATED_BODY()

public:
	ASimAg_QueueVolume();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void PostInitializeComponents() override;
	//~ End AActor

	/**
	 * Join the queue (idempotent — re-joining returns the existing position). AUTHORITY ONLY: returns
	 * INDEX_NONE on clients. @return the agent's 0-based position in the queue.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Crowd")
	int32 Enqueue(FSeam_EntityId Agent);

	/** Leave the queue. AUTHORITY ONLY. @return true if the agent was queued. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Crowd")
	bool Dequeue(FSeam_EntityId Agent);

	/** 0-based position of Agent in the queue, or INDEX_NONE if not queued. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	int32 GetPosition(FSeam_EntityId Agent) const;

	/** World stand-position for Agent's current queue position. Returns the actor location if not queued. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	FVector GetSlotLocation(FSeam_EntityId Agent) const;

	/** Number of agents currently queued. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	int32 GetQueueLength() const { return Queue.Slots.Num(); }

	/** The agent at the head of the queue (next to be served), or invalid if empty. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	FSeam_EntityId GetHead() const;

	/** Fired when the queue changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Crowd")
	FSimAg_OnQueueChanged OnQueueChanged;

	/** Called by the fast-array callbacks on clients to surface a replicated queue change. */
	void HandleReplicatedChange();

private:
	/** Replicated queue (delta-serialized). */
	UPROPERTY(Replicated)
	FSimAg_QueueArray Queue;

	/** Monotonic ticket source (authority only). */
	int64 NextTicket = 1;

	/** Cached slot spacing (world units) from settings. */
	float SlotSpacing = 100.f;

	/** Wake the actor from net dormancy so a just-changed delta replicates this frame. */
	void WakeForChange();

	/** Sort slots by Ticket so positions are stable and in arrival order. */
	void SortByTicket();
};
