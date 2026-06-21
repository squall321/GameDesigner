// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "FSM/DPState.h"
#include "GameplayTagContainer.h"
#include "Move_TransitionGuards.generated.h"

/**
 * Guard: passes when the owner is currently airborne (CMC IsFalling). Used on transitions INTO air states.
 * Side-effect free (UDP_TransitionGuard contract).
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_AirborneGuard : public UDP_TransitionGuard
{
	GENERATED_BODY()

public:
	/** If true the guard inverts (passes when GROUNDED). */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard")
	bool bRequireGrounded = false;

	virtual bool EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const override;
};

/**
 * Guard: passes when the seam reports non-zero move intent (or sprint/crouch held), per the configured
 * intent flag. Drives Walk->Run, Walk->Crouch, etc. without the state caching input.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_IntentGuard : public UDP_TransitionGuard
{
	GENERATED_BODY()

public:
	/** Which intent to test. */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard")
	bool bRequireSprintHeld = false;

	/** Require crouch held. */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard")
	bool bRequireCrouchHeld = false;

	/** Require non-zero movement input above this magnitude. <= 0 disables the magnitude test. */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard", meta = (ClampMin = "0.0"))
	float MinMoveMagnitude = 0.f;

	/** Invert the whole result (passes when the requirement is NOT met). */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard")
	bool bInvert = false;

	virtual bool EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const override;
};

/**
 * Guard: passes when the stamina component reports enough stamina (and is not exhausted). Used on
 * sprint/dash transitions. With no stamina component present it passes (unrestricted fallback).
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_StaminaGuard : public UDP_TransitionGuard
{
	GENERATED_BODY()

public:
	/** Minimum absolute stamina required to pass. 0 = "any positive stamina". */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard", meta = (ClampMin = "0.0"))
	float MinStamina = 0.f;

	/** If true the guard FAILS while the exhaustion lockout is active. */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard")
	bool bBlockWhileExhausted = true;

	virtual bool EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const override;
};

/**
 * Guard: passes when a trace-driven ledge is currently available in front of the character, optionally
 * filtered to vault-only or mantle-only. Reads the active locomotion profile / settings for trace tuning.
 * Side-effect free (the trace is a read-only query).
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_LedgeAvailableGuard : public UDP_TransitionGuard
{
	GENERATED_BODY()

public:
	/** 0 = any ledge, 1 = vault-only, 2 = mantle-only. */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard", meta = (ClampMin = "0", ClampMax = "2"))
	int32 LedgeKindFilter = 0;

	virtual bool EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const override;
};

/**
 * Guard: passes when the world-time-stamped phase timer (PhaseEndTime blackboard key) has ELAPSED.
 * Drives the self-exit of timed states (Dash / WallRun). Side-effect free.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_PhaseElapsedGuard : public UDP_TransitionGuard
{
	GENERATED_BODY()

public:
	/** If true the guard inverts (passes while the phase is STILL running). */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard")
	bool bInvert = false;

	virtual bool EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const override;
};

/**
 * Guard: passes when an interpolated traversal (mantle/vault) has finished — i.e. world time minus the
 * traversal start exceeds the traversal duration on the blackboard. Drives Mantle/Vault back to ground.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_TraversalCompleteGuard : public UDP_TransitionGuard
{
	GENERATED_BODY()

public:
	virtual bool EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const override;
};

/**
 * Guard: passes when a server-confirmed special move is pending and its request tag matches the
 * configured tag. Reads the one-shot token written by the intent component on authority (and replicated
 * implicitly through the FSM's confirmed-entry path). Side-effect free — it only READS the token; the
 * token is cleared by the intent component / target state on entry.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_SpecialMoveConfirmedGuard : public UDP_TransitionGuard
{
	GENERATED_BODY()

public:
	/** The special-move request tag this transition responds to (Move.Request.Dash, ...). */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard", meta = (Categories = "Move.Request"))
	FGameplayTag RequestTag;

	virtual bool EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const override;
};

/**
 * Guard: passes when the character is currently inside a water volume (or, inverted, when out of water).
 * Drives entry into / exit from the Swim state.
 */
UCLASS()
class DESIGNPATTERNSMOVEMENT_API UMove_InWaterGuard : public UDP_TransitionGuard
{
	GENERATED_BODY()

public:
	/** If true the guard inverts (passes when OUT of water). */
	UPROPERTY(EditAnywhere, Category = "Movement|Guard")
	bool bRequireOutOfWater = false;

	virtual bool EvaluateGuard_Implementation(const UDP_StateMachineComponent* OwningComponent) const override;
};
