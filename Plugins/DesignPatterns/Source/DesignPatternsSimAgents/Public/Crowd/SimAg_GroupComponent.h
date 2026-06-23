// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "SimAg_GroupComponent.generated.h"

class USimAg_FormationSubsystem;

/** Fired (server and clients) when this agent's group membership changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimAg_OnGroupChanged, USimAg_GroupComponent*, GroupComponent);

/**
 * Membership of one agent in a movement GROUP/formation. Holds the replicated GroupId and a leader flag,
 * and resolves the agent's formation slot offset from USimAg_FormationSubsystem so steering targets a
 * slot-relative point behind the leader instead of all members stacking on one goal.
 *
 * REPLICATION: GroupId (ReplicatedUsing) and bIsLeader (Replicated) are the only authoritative state and
 * change rarely; the slot offset is derived locally. Authority assigns membership; mutators guard at top.
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_GroupComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimAg_GroupComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	/** Assign this agent to a group (or invalid to leave). AUTHORITY ONLY: early-returns on clients. */
	UFUNCTION(BlueprintCallable, Category = "SimAgents|Crowd")
	void SetGroup(FSeam_EntityId InGroupId, bool bAsLeader);

	/** The agent's current group id (invalid if ungrouped). Client-safe (replicated). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	FSeam_EntityId GetGroupId() const { return GroupId; }

	/** True if this agent is the group leader. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	bool IsLeader() const { return bIsLeader; }

	/**
	 * The world stand-position for this agent's formation slot, given the leader's goal. Leaders return
	 * LeaderGoal unchanged; followers return their slot offset behind it (oriented toward LeaderGoal from
	 * the agent's current position). Returns LeaderGoal when ungrouped.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Crowd")
	FVector GetFormationSlotWorld(const FVector& LeaderGoal) const;

	/** Formation pattern this group uses (a USimAg_FormationAsset DataTag). Authored per group archetype. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimAgents|Crowd")
	FGameplayTag FormationTag;

	/** Fired when group membership changes (server / client replication). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Crowd")
	FSimAg_OnGroupChanged OnGroupChanged;

protected:
	/** OnRep for the replicated group id: refresh the cached slot and fire the delegate on clients. */
	UFUNCTION()
	void OnRep_Group();

private:
	/** Authoritative group id (replicated). */
	UPROPERTY(ReplicatedUsing = OnRep_Group)
	FSeam_EntityId GroupId;

	/** Whether this agent leads the group (replicated). */
	UPROPERTY(Replicated)
	bool bIsLeader = false;

	/** Cached slot index assigned by the formation subsystem (transient; re-derived if stale). */
	int32 CachedSlotIndex = 0;

	/** Resolve the agent's stable id off a sibling agent component; invalid if none. */
	FSeam_EntityId ResolveAgentId() const;

	/** Resolve the world formation subsystem (weak/null-safe). */
	USimAg_FormationSubsystem* GetFormationSubsystem() const;

	/** (Re)assign this agent's slot index in the formation subsystem. */
	void RefreshSlot();
};
