// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Move_LocomotionProfile.generated.h"

/**
 * Per-domain movement tuning (one row of a UMove_LocomotionProfile). Bundles every CMC-facing number
 * a state needs so a state never hardcodes a speed/acceleration — it reads the row for its motion
 * domain from the active profile. A field left <= 0 means "use the project's Move_DeveloperSettings
 * fallback" (documented defensive default), so partial authoring still produces sensible motion.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMOVEMENT_API FMove_DomainTuning
{
	GENERATED_BODY()

	/** Target MaxWalkSpeed (cm/s) the CMC is driven to in this domain. <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float MaxSpeed = 0.f;

	/** Ground/braking acceleration (cm/s^2). <= 0 -> leave the CMC default untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float Acceleration = 0.f;

	/** Braking deceleration (cm/s^2) for this domain. <= 0 -> leave the CMC default untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float BrakingDeceleration = 0.f;

	/** Ground friction multiplier for this domain. <= 0 -> leave the CMC default untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement", meta = (ClampMin = "0.0"))
	float GroundFriction = 0.f;
};

/**
 * Authored-once locomotion tuning asset for a character archetype (player, heavy NPC, agile NPC, ...).
 *
 * The UMove_MovementComponent points at one of these; its states read the per-domain tuning rows and
 * the named tunables below instead of carrying their own numbers. Because UDP_State objects are STATELESS
 * and shared, ALL gameplay numbers must live in data — this asset is that data. Multiple movement
 * components may share one profile.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSMOVEMENT_API UMove_LocomotionProfile : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	// ---- Ground domains ----

	/** Tuning for the Walk state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground")
	FMove_DomainTuning Walk;

	/** Tuning for the Run state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground")
	FMove_DomainTuning Run;

	/** Tuning for the Sprint state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground")
	FMove_DomainTuning Sprint;

	/** Tuning for the Crouch state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Ground")
	FMove_DomainTuning Crouch;

	// ---- Air ----

	/** Air control [0,1] applied while airborne (jump/dash). 0 -> leave CMC default untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirControl = 0.35f;

	/** Jump launch Z velocity (cm/s). <= 0 -> leave ACharacter::JumpZVelocity untouched. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float JumpZVelocity = 0.f;

	/** Number of mid-air jumps allowed. < 0 -> use the project settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "-1"))
	int32 AirJumpBudget = -1;

	/** Z velocity (cm/s) applied to a double jump. <= 0 -> reuse JumpZVelocity / character default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Air", meta = (ClampMin = "0.0"))
	float DoubleJumpZVelocity = 0.f;

	// ---- Water ----

	/** Tuning for the Swim state (MaxSpeed maps to CMC MaxSwimSpeed). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Water")
	FMove_DomainTuning Swim;

	// ---- Slide ----

	/** Floor angle (deg) above which a slide accelerates downhill. <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float SlideSlopeThresholdDeg = 0.f;

	/** Minimum slide speed (cm/s); below it the slide ends. <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = "0.0"))
	float SlideMinSpeed = 0.f;

	/** Extra downhill acceleration (cm/s^2) scaled by slope steepness during a slide. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Slide", meta = (ClampMin = "0.0"))
	float SlideSlopeAcceleration = 800.f;

	// ---- Wall-run ----

	/** Maximum wall-run duration (seconds). <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun", meta = (ClampMin = "0.0"))
	float WallRunDuration = 0.f;

	/** Lateral wall-detection trace distance (cm). <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun", meta = (ClampMin = "0.0"))
	float WallDetectDistance = 0.f;

	/** Upward "stick" velocity (cm/s) that counters gravity during a wall-run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "WallRun")
	float WallRunGravityScale = 0.25f;

	// ---- Climb ----

	/** Climb speed (cm/s). <= 0 -> reuse Walk speed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Climb", meta = (ClampMin = "0.0"))
	float ClimbSpeed = 0.f;

	/** Surface trace types that count as a wall/ledge for traversal & wall-run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Traversal")
	TArray<TEnumAsByte<ECollisionChannel>> TraversalTraceChannels;

	/** Resolve a domain's MaxSpeed, falling back to settings when the row leaves it at 0. */
	float ResolveMaxSpeed(const FMove_DomainTuning& Domain, float SettingsFallback) const;
};
