// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "SimAg_AgentIntentComponent.generated.h"

/**
 * Player-owned client->server intent funnel for sim-agent actions. A controlling client never mutates
 * authoritative state directly; instead it calls these Server RPCs, which validate ownership/authority,
 * DERIVE the agent id from the owner's USimAg_AgentComponent::GetAgentId() (never trusting a client-sent
 * id), and route the request to the world job board / reservation router on the server.
 *
 * This keeps the no-direct-client-mutation rule intact: the board's ClaimJobForAgent and the reservation
 * seam's TryReserve run only on authority, reached here after server-side validation.
 *
 * SetIsReplicatedByDefault(true) is for the RPC plumbing only; this component holds NO replicated
 * authoritative UPROPERTYs.
 */
UCLASS(ClassGroup = (DesignPatternsSimAgents), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMAGENTS_API USimAg_AgentIntentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimAg_AgentIntentComponent();

	/**
	 * Client intent: claim the best open job of JobKind near Location for THIS agent. Validated and run on
	 * the server; the agent id is derived server-side from the owning agent component.
	 */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "SimAgents|Intent")
	void Server_RequestClaimJob(FGameplayTag JobKind, FVector Location);

	/**
	 * Client intent: reserve Target for THIS agent (so a haul/job target is claimed once). Validated and
	 * run on the server; the agent id is derived server-side.
	 */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "SimAgents|Intent")
	void Server_RequestReserve(FSeam_EntityId Target);

	/** Client intent: release a reservation this agent holds on Target. Validated server-side. */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "SimAgents|Intent")
	void Server_RequestRelease(FSeam_EntityId Target);

private:
	/** The stable id of the owning agent, derived from the owner's agent component (invalid if none). */
	FSeam_EntityId ResolveOwnerAgentId() const;
};
