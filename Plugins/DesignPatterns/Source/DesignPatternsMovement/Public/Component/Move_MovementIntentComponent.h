// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Move/Seam_MovementController.h"
#include "Move_MovementIntentComponent.generated.h"

class UMove_MovementComponent;
class UMove_StaminaComponent;
class UDP_GameplayActionComponent;
class ACharacter;

/**
 * The PLAYER-OWNED movement driver: implements ISeam_MovementController so the movement state machine is
 * driven identically for players and AI, and carries the client->server intent path for special moves.
 *
 * Local intent (move/facing/sprint/crouch) is set by the project's input layer via the setters here and
 * read by the movement component through the seam — cosmetic and predictive, no replication.
 *
 * Special moves (dash/dodge/mantle/vault) follow HARD RULE 4's authority pattern:
 *   - The local player requests one (RequestSpecialMove) -> queued as a one-shot the movement component
 *     consumes via ConsumeSpecialMoveRequest for IMMEDIATE cosmetic prediction.
 *   - In parallel, ServerRequestSpecialMove (WithValidation) runs on the server: it re-derives the
 *     pre-conditions (cooldown via the action component, stamina via ISeam_NeedProvider, and re-runs the
 *     traversal trace for mantle/vault) and, if valid, stamps a server-confirmed token on the FSM
 *     blackboard + drives ChangeState on authority. The confirmed token is what the
 *     UMove_SpecialMoveConfirmedGuard reads, so the AUTHORITATIVE entry is always server-gated even though
 *     the client predicted the cosmetic part.
 *
 * This component must be on a connection-owning actor (the player's pawn) for the server RPCs to route;
 * it self-checks ownership and logs once if misused.
 */
UCLASS(ClassGroup = (DesignPatternsMovement), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSMOVEMENT_API UMove_MovementIntentComponent : public UActorComponent, public ISeam_MovementController
{
	GENERATED_BODY()

public:
	UMove_MovementIntentComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	//~ End UActorComponent

	//~ Begin ISeam_MovementController
	virtual FVector GetMoveIntent_Implementation() const override { return MoveIntent; }
	virtual FRotator GetFacingIntent_Implementation() const override { return FacingIntent; }
	virtual bool IsSprintHeld_Implementation() const override { return bSprintHeld; }
	virtual bool IsCrouchHeld_Implementation() const override { return bCrouchHeld; }
	virtual bool ConsumeSpecialMoveRequest_Implementation(FGameplayTag& OutRequestTag) override;
	//~ End ISeam_MovementController

	// ---- Local intent setters (called by the project's input layer) ----

	/** Set the desired move direction/magnitude in world space. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Intent")
	void SetMoveIntent(const FVector& InIntent) { MoveIntent = InIntent; }

	/** Set the desired facing rotation. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Intent")
	void SetFacingIntent(const FRotator& InFacing) { FacingIntent = InFacing; }

	/** Set the sprint-held flag. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Intent")
	void SetSprintHeld(bool bHeld) { bSprintHeld = bHeld; }

	/** Set the crouch-held flag. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Intent")
	void SetCrouchHeld(bool bHeld) { bCrouchHeld = bHeld; }

	/**
	 * Request a special move. Queues a one-shot for local cosmetic prediction (consumed by the movement
	 * component) AND fires the server RPC for authoritative admission. Call on the owning client.
	 * @param RequestTag one of Move.Request.* (Dash/Dodge/Mantle/Vault/...).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Movement|Intent")
	void RequestSpecialMove(FGameplayTag RequestTag);

protected:
	/**
	 * Server RPC: validate a special-move request and, if valid, drive authoritative entry. The server
	 * re-derives cooldown/stamina/trace pre-conditions and never trusts the client's claim. The optional
	 * Context carries a predicted target (e.g. the client's mantle target) the server may sanity-check;
	 * the server re-traces and uses its own result.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerRequestSpecialMove(FGameplayTag RequestTag, FSeam_NetValue Context);

private:
	/** Desired move direction/magnitude (world space). Local-only, not replicated. */
	FVector MoveIntent = FVector::ZeroVector;

	/** Desired facing. Local-only. */
	FRotator FacingIntent = FRotator::ZeroRotator;

	/** Sprint held flag. Local-only. */
	bool bSprintHeld = false;

	/** Crouch held flag. Local-only. */
	bool bCrouchHeld = false;

	/** The pending one-shot special-move request for local prediction (invalid = none). */
	FGameplayTag PendingRequest;

	/** Logged-once flag for the ownership misuse warning. */
	bool bLoggedOwnershipWarning = false;

	/** Resolve the co-located movement state machine component. */
	UMove_MovementComponent* ResolveMovementComponent() const;

	/** Resolve the co-located stamina component. */
	UMove_StaminaComponent* ResolveStaminaComponent() const;

	/** Resolve the co-located action component (for cooldown checks + dash grant). */
	UDP_GameplayActionComponent* ResolveActionComponent() const;

	/** @return true if this component is on a connection-owning actor able to send server RPCs. */
	bool HasValidNetOwnership() const;

	/**
	 * Authoritative admission for a validated request. Maps the request tag to a target state, performs
	 * the move-specific server-side derivation (trace for mantle/vault), stamps the confirmed token, and
	 * calls ChangeState. Runs on the server only.
	 */
	void ApplyAuthoritativeSpecialMove(FGameplayTag RequestTag);

	/** Map a Move.Request.* tag to the Move.State.* tag it drives, or an invalid tag if unmapped. */
	static FGameplayTag RequestToStateTag(const FGameplayTag& RequestTag);
};
