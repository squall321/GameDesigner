// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Move_CharacterMovementComponent.generated.h"

/**
 * OPTIONAL predicted CMC subclass with the full saved-move triad, for projects that want a couple of
 * compressed movement flags (Sprint / WantsDash) folded into the engine's client prediction so they are
 * replayed correctly during correction. Projects that do not need predicted custom flags should use the
 * stock UCharacterMovementComponent and drive speed through the replicated MaxWalkSpeed path (the
 * UMove_MovementComponent states already do this); this class is purely additive and never required.
 *
 * Triad:
 *   - FMove_SavedMove_Character  : Clear/SetMoveFor/CanCombineWith/GetCompressedFlags carry the flags.
 *   - FMove_NetworkPredictionData_Client_Character : AllocateNewMove hands out the saved-move type.
 *   - this CMC : UpdateFromCompressedFlags reads the flags back; GetPredictionData_Client supplies the
 *                client prediction data object.
 *
 * The two predicted bits use the engine's reserved custom-flag slots (FLAG_Custom_0/1) so they never
 * collide with the engine's own crouch/jump flags.
 */
UCLASS(ClassGroup = (DesignPatternsMovement), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSMOVEMENT_API UMove_CharacterMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UMove_CharacterMovementComponent();

	//~ Begin UCharacterMovementComponent
	virtual void UpdateFromCompressedFlags(uint8 Flags) override;
	virtual class FNetworkPredictionData_Client* GetPredictionData_Client() const override;
	//~ End UCharacterMovementComponent

	/** Set the locally-desired predicted sprint flag (mirrored into the saved move). */
	void SetWantsToSprint(bool bWants) { bWantsToSprint = bWants; }

	/** Set the locally-desired predicted dash flag (a one-shot intent the saved move carries). */
	void SetWantsToDash(bool bWants) { bWantsToDash = bWants; }

	/** True while the predicted sprint flag is set (post-UpdateFromCompressedFlags on the server). */
	bool WantsToSprint() const { return bWantsToSprint; }

	/** True while the predicted dash flag is set. */
	bool WantsToDash() const { return bWantsToDash; }

protected:
	/** Predicted sprint intent (replayed via compressed flag FLAG_Custom_0). */
	uint8 bWantsToSprint : 1;

	/** Predicted dash intent (replayed via compressed flag FLAG_Custom_1). */
	uint8 bWantsToDash : 1;

	// The saved-move and prediction-data types need access to the predicted flags.
	friend class FMove_SavedMove_Character;
};

/**
 * Saved move carrying the two predicted movement flags. Mirrors the engine's FSavedMove_Character with
 * the standard four overrides so corrections replay sprint/dash deterministically.
 */
class DESIGNPATTERNSMOVEMENT_API FMove_SavedMove_Character : public FSavedMove_Character
{
public:
	typedef FSavedMove_Character Super;

	/** Reset all saved fields to defaults before reuse from the pool. */
	virtual void Clear() override;

	/** Pack the predicted flags into the compressed-flags byte sent to the server. */
	virtual uint8 GetCompressedFlags() const override;

	/** Two moves may combine only if their predicted flags match (so a flag edge is never dropped). */
	virtual bool CanCombineWith(const FSavedMovePtr& NewMove, ACharacter* Character, float MaxDelta) const override;

	/** Capture the predicted flags from the CMC at the time this move is recorded. */
	virtual void SetMoveFor(ACharacter* Character, float InDeltaTime, FVector const& NewAccel,
		class FNetworkPredictionData_Client_Character& ClientData) override;

	/** Predicted sprint flag captured for this move. */
	uint8 bSavedWantsToSprint : 1;

	/** Predicted dash flag captured for this move. */
	uint8 bSavedWantsToDash : 1;
};

/** Client prediction data that hands out FMove_SavedMove_Character instances. */
class DESIGNPATTERNSMOVEMENT_API FMove_NetworkPredictionData_Client_Character : public FNetworkPredictionData_Client_Character
{
public:
	typedef FNetworkPredictionData_Client_Character Super;

	explicit FMove_NetworkPredictionData_Client_Character(const UCharacterMovementComponent& ClientMovement);

	/** Allocate a movement-module saved move. */
	virtual FSavedMovePtr AllocateNewMove() override;
};
