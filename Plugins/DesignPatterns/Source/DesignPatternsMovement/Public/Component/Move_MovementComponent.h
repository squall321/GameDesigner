// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FSM/DPStateMachineComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Move_MovementComponent.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class UMove_LocomotionProfile;
class UMove_StaminaComponent;
class ISeam_MovementController;

/**
 * The locomotion state machine: a UDP_StateMachineComponent specialization that drives an engine
 * character through the movement states (Walk/Run/Sprint/Crouch/Slide/Jump/DoubleJump/Dash/WallRun/
 * Climb/Mantle/Vault/Swim).
 *
 * It reuses ALL of the base FSM machinery (shared Definition, per-instance Blackboard, replicated
 * ActiveStateTag, transition evaluation) and adds three things:
 *   1. Cached typed owner objects — the ACharacter and its UCharacterMovementComponent are resolved
 *      once and held WEAKLY here (states reach them through this component, never caching them
 *      themselves, because states are stateless/shared).
 *   2. The active UMove_LocomotionProfile, surfaced so states read tuning from data.
 *   3. The resolved ISeam_MovementController, so the machine is driven identically by a player input
 *      component and an AI driver. Intent is read through the seam; the component never assumes a
 *      controller type.
 *
 * Replication note: nothing new replicates here — only the inherited ActiveStateTag crosses the wire.
 * The states themselves do cosmetic/predicted CMC work locally; authoritative special-move admission is
 * handled by the player-owned UMove_MovementIntentComponent + the SpecialMoveConfirmed blackboard token.
 */
UCLASS(ClassGroup = (DesignPatternsMovement), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSMOVEMENT_API UMove_MovementComponent : public UDP_StateMachineComponent
{
	GENERATED_BODY()

public:
	UMove_MovementComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/** The locomotion tuning profile every state reads its numbers from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Movement")
	TObjectPtr<UMove_LocomotionProfile> LocomotionProfile;

	/** @return the cached owning ACharacter (re-resolved lazily if the weak ptr is stale). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Movement")
	ACharacter* GetCharacter() const;

	/** @return the cached UCharacterMovementComponent (re-resolved lazily if stale). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Movement")
	UCharacterMovementComponent* GetCharacterMovement() const;

	/** @return the active locomotion profile (may be null; callers fall back to settings). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Movement")
	UMove_LocomotionProfile* GetProfile() const { return LocomotionProfile; }

	/**
	 * @return the resolved movement-intent provider, or an empty interface. Resolution order:
	 *   the owning Pawn's Controller (Implements<>), then the owner actor, then a co-located
	 *   component implementing the seam. Cached weakly and re-resolved when stale.
	 */
	TScriptInterface<ISeam_MovementController> GetMovementController();

	/** @return a co-located stamina component on the owner (cached weakly), or null. */
	UMove_StaminaComponent* GetStaminaComponent() const;

	/**
	 * Read the current move intent through the seam (zero if no controller). Convenience used widely
	 * by states; safe to call every tick.
	 */
	FVector GetMoveIntent();

	/** Read sprint-held through the seam (false if no controller). */
	bool IsSprintHeld();

	/** Read crouch-held through the seam (false if no controller). */
	bool IsCrouchHeld();

	/** Read facing intent through the seam (owner rotation if no controller). */
	FRotator GetFacingIntent();

	/**
	 * Poll the seam for a pending special-move request, writing the request tag. Returns true and
	 * consumes the request (one-shot). States/guards use this to drive dash/mantle/vault entry.
	 */
	bool ConsumeSpecialMoveRequest(FGameplayTag& OutRequestTag);

private:
	/** Non-owning cached owner character. The owner outlives this component, but re-resolved defensively. */
	UPROPERTY(Transient)
	TWeakObjectPtr<ACharacter> CachedCharacter;

	/** Non-owning cached CMC. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UCharacterMovementComponent> CachedCMC;

	/** Non-owning cached stamina component. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UMove_StaminaComponent> CachedStamina;

	/** Non-owning cached movement-controller provider object (interface re-wrapped on access). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> CachedControllerObject;

	/** Resolve & cache the ACharacter / CMC / stamina from the owner. Idempotent. */
	void CacheOwnerObjects();
};
