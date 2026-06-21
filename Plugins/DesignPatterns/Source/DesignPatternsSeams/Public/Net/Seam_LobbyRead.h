// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_LobbyRead.generated.h"

/** Connection / readiness phase of one player slot in a pre-match lobby. */
UENUM(BlueprintType)
enum class ESeam_LobbyMemberState : uint8
{
	/** Slot is empty / unused. */
	Empty,
	/** Player connected but not yet ready. */
	Joined,
	/** Player has pressed ready-up. */
	Ready,
	/** Player is the host (always counts as authoritative; readiness implied). */
	Host,
	/** Player temporarily disconnected; awaiting host-migration / reconnection. */
	Reconnecting
};

/**
 * One read-only lobby roster row, flat and net/save-safe (no object refs). Mirrors the shape the
 * replicated lobby carrier exposes so a UI / matchmaking layer can render the roster without
 * depending on the Net module's concrete carrier type.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_LobbyMember
{
	GENERATED_BODY()

	/** Stable per-connection slot id (lobby-local; survives a transient reconnect). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net|Lobby")
	int32 SlotId = INDEX_NONE;

	/** Display name of the player occupying this slot. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net|Lobby")
	FString PlayerName;

	/** Team the player has been assigned to (empty = unassigned / free-for-all). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net|Lobby")
	FGameplayTag TeamTag;

	/** Party the player belongs to (empty = solo). Party members are kept on the same team. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net|Lobby")
	FGameplayTag PartyTag;

	/** Connection / readiness state of the slot. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net|Lobby")
	ESeam_LobbyMemberState State = ESeam_LobbyMemberState::Empty;

	/** Measured ping in milliseconds (-1 if unknown). Advisory; never trusted for gameplay. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Net|Lobby")
	int32 PingMs = -1;

	bool IsOccupied() const { return State != ESeam_LobbyMemberState::Empty; }
	bool IsReady() const { return State == ESeam_LobbyMemberState::Ready || State == ESeam_LobbyMemberState::Host; }
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_LobbyRead : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read seam for the pre-match lobby / party / matchmaking state. Implemented by a GameState-referenced
 * replicated carrier (ANet_LobbyState) so the roster, team assignments, ready-state and host-migration
 * awareness read correctly on every client. UI, the match-start flow and analytics read through this
 * without depending on the Net module's concrete lobby actor. All WRITES (ready-up, team assignment,
 * party invites) are authority-only and live on the carrier + player-owned intent components.
 */
class DESIGNPATTERNSSEAMS_API ISeam_LobbyRead
{
	GENERATED_BODY()

public:
	/** Full lobby roster (occupied and empty slots, in slot order). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Lobby")
	void GetLobbyMembers(TArray<FSeam_LobbyMember>& OutMembers) const;

	/** Number of currently-occupied slots. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Lobby")
	int32 GetOccupiedSlotCount() const;

	/** True when every occupied slot is Ready/Host and the minimum player count is met. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Lobby")
	bool AreAllPlayersReady() const;

	/** The host slot's id (INDEX_NONE if no host is resolved — e.g. mid host-migration). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Lobby")
	int32 GetHostSlotId() const;

	/** True while the lobby is recovering from a host disconnect (host-migration in progress). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Lobby")
	bool IsMigratingHost() const;

	/** A free-form lobby phase tag (e.g. DP.Net.Lobby.Phase.Filling / Countdown / Starting). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Net|Lobby")
	FGameplayTag GetLobbyPhase() const;
};
