// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_LobbyRead.h"
#include "Lobby/FNet_LobbyTypes.h"
#include "ANet_LobbyState.generated.h"

/** Fired (server + clients) whenever the lobby roster / ready-state / phase changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FNet_OnLobbyChanged);

/**
 * Replicated authority carrier for the pre-match lobby / party / matchmaking state, implementing the read
 * seam ISeam_LobbyRead so UI, the match-start flow and analytics read the roster through the seam without
 * depending on this concrete class.
 *
 * It is an AInfo (bReplicates + bAlwaysRelevant), server-spawned and referenced from a replicated
 * GameState UPROPERTY so every client resolves it. The roster is a delta-replicated fast array; scalar
 * lobby state (phase, host slot, migrating flag) is ReplicatedUsing=OnRep. All WRITES are authority-only
 * and routed here; clients express intent through UNet_PartyComponent (player-owned, Server...WithValidation).
 *
 * Host-migration awareness: when the host's connection drops, the server (on a listen host this is moot;
 * for a travelled-host topology the new authority) marks the lobby migrating, holds slots in the
 * Reconnecting state for a grace window, and re-binds a returning player by PlayerNetId.
 *
 * The carrier holds NO matchmaking policy (team-balance rules, min players) — those live in the owning
 * GameMode/flow which drives this carrier's authority mutators.
 */
UCLASS()
class DESIGNPATTERNSNET_API ANet_LobbyState : public AInfo, public ISeam_LobbyRead
{
	GENERATED_BODY()

public:
	ANet_LobbyState();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor

	// ---- ISeam_LobbyRead (read; safe on clients) ----
	virtual void GetLobbyMembers_Implementation(TArray<FSeam_LobbyMember>& OutMembers) const override;
	virtual int32 GetOccupiedSlotCount_Implementation() const override;
	virtual bool AreAllPlayersReady_Implementation() const override;
	virtual int32 GetHostSlotId_Implementation() const override;
	virtual bool IsMigratingHost_Implementation() const override;
	virtual FGameplayTag GetLobbyPhase_Implementation() const override;

	// ---- Authority mutators (each early-returns on clients) ----

	/**
	 * Add a player to the next free slot (or re-bind an existing slot by net id after a reconnect).
	 * AUTHORITY ONLY. @return the slot id, or INDEX_NONE on failure (lobby full).
	 */
	int32 AddOrRebindPlayer(const FString& PlayerNetId, const FString& PlayerName, bool bIsHost);

	/** Remove a player's slot (full leave). AUTHORITY ONLY. @return true if a slot was removed. */
	bool RemovePlayer(const FString& PlayerNetId);

	/** Mark a slot Reconnecting (transient drop) without freeing it, for host-migration grace. AUTHORITY ONLY. */
	bool MarkReconnecting(const FString& PlayerNetId);

	/** Set a slot's ready flag. AUTHORITY ONLY. @return true if the slot exists and changed. */
	bool SetReady(int32 SlotId, bool bReady);

	/** Assign a slot to a team (party members are moved together by the caller). AUTHORITY ONLY. */
	bool AssignTeam(int32 SlotId, FGameplayTag TeamTag);

	/** Set a slot's party tag (for invite acceptance). AUTHORITY ONLY. */
	bool SetParty(int32 SlotId, FGameplayTag PartyTag);

	/** Set the lobby phase (Filling/Countdown/Starting). AUTHORITY ONLY. */
	void SetLobbyPhase(FGameplayTag PhaseTag);

	/** Begin / end the host-migration state. AUTHORITY ONLY. */
	void SetMigratingHost(bool bMigrating);

	// ---- Reads (client-safe) ----

	/** Find a slot by net id, or null. */
	const FNet_LobbyPlayerItem* FindByNetId(const FString& PlayerNetId) const;

	/** Find a slot by id, or null. */
	const FNet_LobbyPlayerItem* FindBySlot(int32 SlotId) const;

	/** Minimum occupied + ready players required for AreAllPlayersReady to return true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Lobby", meta = (ClampMin = "1"))
	int32 MinPlayersToStart = 2;

	/** Maximum slots the lobby allows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Net|Lobby", meta = (ClampMin = "1"))
	int32 MaxSlots = 8;

	/** Fired (server + clients) when the lobby changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Net|Lobby")
	FNet_OnLobbyChanged OnLobbyChanged;

	/** Called by the fast-array item callbacks on clients to surface a replicated roster change. */
	void HandleReplicatedRosterChange();

private:
	/** Mutable slot find for authority mutators. */
	FNet_LobbyPlayerItem* FindByNetIdMutable(const FString& PlayerNetId);
	FNet_LobbyPlayerItem* FindBySlotMutable(int32 SlotId);

	/** Allocate the lowest free slot id, or INDEX_NONE if the lobby is full. */
	int32 AllocateSlotId() const;

	/** Self-register under DP.Service.Net.Lobby (WeakObserved) so consumers resolve ISeam_LobbyRead. */
	void RegisterSelfAsService();

	/** Mark the fast array dirty and broadcast + bus a roster-changed notification (authority side). */
	void NotifyRosterChanged();

	/** Wake from net dormancy so a just-changed delta replicates this frame. */
	void WakeForChange();

	/** Replicated roster (delta-serialized fast array). */
	UPROPERTY(Replicated)
	FNet_LobbyPlayerArray Roster;

	/** Replicated lobby phase. */
	UPROPERTY(ReplicatedUsing = OnRep_ScalarState)
	FGameplayTag LobbyPhase;

	/** Replicated host slot id (INDEX_NONE if unresolved). */
	UPROPERTY(ReplicatedUsing = OnRep_ScalarState)
	int32 HostSlotId = INDEX_NONE;

	/** Replicated host-migration flag. */
	UPROPERTY(ReplicatedUsing = OnRep_ScalarState)
	bool bMigratingHost = false;

	/** OnRep for the scalar lobby state (phase/host/migrating): surface a lobby change. */
	UFUNCTION()
	void OnRep_ScalarState();
};
