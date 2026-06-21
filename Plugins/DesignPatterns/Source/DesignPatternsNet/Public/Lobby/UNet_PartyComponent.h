// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UNet_PartyComponent.generated.h"

class ANet_LobbyState;

/**
 * Player-owned client->server intent component for the lobby. Lives on the player's controller / pawn /
 * player state (a connection-owned actor) so its Server RPCs are routed by the owning client only. It
 * never holds authoritative state: every request is a Server...WithValidation that the server re-checks
 * against the replicated ANet_LobbyState before mutating it. This keeps the lobby carrier free of client
 * RPCs (a carrier is not connection-owned) while still letting each client express ready-up / team / party
 * intent — the canonical "intent on a player-owned component, state on a carrier" split.
 *
 * The server resolves THIS player's slot from the owning connection (never trusting a client-supplied slot
 * id for identity), so a client can only affect its own slot for ready/party, and team/invite requests are
 * validated against lobby policy on the server.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSNET_API UNet_PartyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNet_PartyComponent();

	// ---- Public API (call on the owning client; routes to the server) ----

	/** Request to toggle this player's ready flag. Server resolves the caller's slot and validates. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Lobby")
	void RequestReady(bool bReady);

	/** Request to join a team. Server validates against team-balance policy before assigning. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Lobby")
	void RequestJoinTeam(FGameplayTag TeamTag);

	/**
	 * Invite another player (by lobby slot id) into this player's party. Server validates both slots exist
	 * and the inviter has room, then tags the invitee's party (acceptance flow is project-defined; the
	 * default treats an invite as an immediate party assignment for the simple case).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Lobby")
	void RequestPartyInvite(int32 TargetSlotId);

	/** Request to leave the current party (clears this player's party tag). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Net|Lobby")
	void RequestLeaveParty();

protected:
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestReady(bool bReady);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestJoinTeam(FGameplayTag TeamTag);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestPartyInvite(int32 TargetSlotId);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestLeaveParty();

private:
	/** Resolve the replicated lobby state from the service locator (ISeam_LobbyRead provider), or null. */
	ANet_LobbyState* ResolveLobby() const;

	/**
	 * Resolve THIS connection's lobby slot id on the SERVER. Derives a stable net-id string from the owning
	 * player state and matches it against the roster — never trusts a client-supplied id for identity.
	 * @return the caller's slot id, or INDEX_NONE if unresolved.
	 */
	int32 ResolveCallerSlotId(ANet_LobbyState* Lobby) const;

	/** A stable net-id string for the owning player (for slot identity / reconnection). */
	FString GetOwnerNetIdString() const;
};
