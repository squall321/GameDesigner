// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_LobbyRead.h"
#include "FNet_LobbyTypes.generated.h"

class ANet_LobbyState;

/**
 * One replicated lobby slot, a fast-array item so individual slot changes (join, ready, team) delta-
 * replicate instead of resending the whole roster. Mirrors FSeam_LobbyMember's shape so the carrier can
 * hand seam rows straight to ISeam_LobbyRead consumers. All fields are plain replicable value types
 * (no object refs / FInstancedStruct), per the net rules.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_LobbyPlayerItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Stable per-connection lobby slot id (survives a transient reconnect). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Lobby")
	int32 SlotId = INDEX_NONE;

	/** Unique net id string of the occupying player (server-authoritative; used to re-bind on reconnect). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Lobby")
	FString PlayerNetId;

	/** Display name. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Lobby")
	FString PlayerName;

	/** Assigned team tag (empty = unassigned). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Lobby")
	FGameplayTag TeamTag;

	/** Party tag (empty = solo). Party members are kept together when auto-balancing teams. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Lobby")
	FGameplayTag PartyTag;

	/** Connection / readiness state. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Lobby")
	ESeam_LobbyMemberState State = ESeam_LobbyMemberState::Empty;

	/** Advisory ping (ms, -1 if unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Lobby")
	int32 PingMs = -1;

	FNet_LobbyPlayerItem() = default;

	//~ FFastArraySerializerItem replication callbacks (client side).
	void PreReplicatedRemove(const struct FNet_LobbyPlayerArray& InArraySerializer);
	void PostReplicatedAdd(const struct FNet_LobbyPlayerArray& InArraySerializer);
	void PostReplicatedChange(const struct FNet_LobbyPlayerArray& InArraySerializer);

	/** Project to the seam row consumers read. */
	FSeam_LobbyMember ToSeamMember() const
	{
		FSeam_LobbyMember M;
		M.SlotId = SlotId;
		M.PlayerName = PlayerName;
		M.TeamTag = TeamTag;
		M.PartyTag = PartyTag;
		M.State = State;
		M.PingMs = PingMs;
		return M;
	}
};

/**
 * Fast-array serializer holding the lobby roster. NetDeltaSerialize forwards to FastArrayDeltaSerialize so
 * only changed slots cross the wire. The owning-carrier back-pointer is non-replicated and set on both
 * server and client so per-item callbacks can notify it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNET_API FNet_LobbyPlayerArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated lobby slots. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Net|Lobby")
	TArray<FNet_LobbyPlayerItem> Players;

	/** Non-replicated back-pointer to the owning carrier for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ANet_LobbyState> OwnerCarrier = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FNet_LobbyPlayerItem, FNet_LobbyPlayerArray>(Players, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the lobby roster array. */
template<>
struct TStructOpsTypeTraits<FNet_LobbyPlayerArray> : public TStructOpsTypeTraitsBase2<FNet_LobbyPlayerArray>
{
	enum { WithNetDeltaSerializer = true };
};
