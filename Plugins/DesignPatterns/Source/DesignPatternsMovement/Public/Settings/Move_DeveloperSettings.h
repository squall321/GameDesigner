// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Move_DeveloperSettings.generated.h"

/**
 * Project-wide configuration for the DesignPatternsMovement module. Appears under
 * Project Settings -> Plugins -> Design Patterns Movement. Edited with no code.
 *
 * These are the genre-neutral DEFENSIVE FALLBACKS used only when a UMove_LocomotionProfile /
 * UMove_StaminaProfile data asset is absent or leaves a field at its "use project default" sentinel.
 * The authoritative, designer-authored tuning lives on those data assets; this settings object exists
 * so the movement component still behaves sanely before a project authors profiles. There are NO
 * hardcoded magic gameplay numbers in the state/component logic — every speed/cost/threshold is read
 * from a profile, falling back to a field here.
 *
 * Kept a PRIVATE module type (not exported via any public Movement header) so the module's API line does
 * not force DeveloperSettings onto consumers.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Movement"))
class UMove_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMove_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Group under the "Plugins" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	//~ End UDeveloperSettings

	/** CDO convenience accessor. Never null in a configured project; callers still null-check. */
	static const UMove_DeveloperSettings* Get();

	// ---- Fallback locomotion speeds (cm/s) ----

	/** Fallback walk speed when no UMove_LocomotionProfile is set. */
	UPROPERTY(EditAnywhere, Config, Category = "Locomotion|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackWalkSpeed = 200.f;

	/** Fallback run speed. */
	UPROPERTY(EditAnywhere, Config, Category = "Locomotion|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackRunSpeed = 400.f;

	/** Fallback sprint speed. */
	UPROPERTY(EditAnywhere, Config, Category = "Locomotion|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackSprintSpeed = 650.f;

	/** Fallback crouch speed. */
	UPROPERTY(EditAnywhere, Config, Category = "Locomotion|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackCrouchSpeed = 150.f;

	/** Fallback swim speed. */
	UPROPERTY(EditAnywhere, Config, Category = "Locomotion|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackSwimSpeed = 300.f;

	// ---- Fallback stamina ----

	/** Fallback maximum stamina when no UMove_StaminaProfile is set. */
	UPROPERTY(EditAnywhere, Config, Category = "Stamina|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackMaxStamina = 100.f;

	/** Fallback stamina regenerated per second while not draining. */
	UPROPERTY(EditAnywhere, Config, Category = "Stamina|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackStaminaRegenPerSecond = 15.f;

	/** Fallback stamina drained per second while sprinting. */
	UPROPERTY(EditAnywhere, Config, Category = "Stamina|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackSprintDrainPerSecond = 10.f;

	/** Fallback flat stamina cost of a dash/dodge. */
	UPROPERTY(EditAnywhere, Config, Category = "Stamina|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackDashStaminaCost = 25.f;

	/** Fallback delay (seconds) after the last drain before regen resumes. */
	UPROPERTY(EditAnywhere, Config, Category = "Stamina|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackRegenDelay = 1.f;

	// ---- Fallback dash ----

	/** Fallback dash duration (seconds). */
	UPROPERTY(EditAnywhere, Config, Category = "Dash|Fallback", meta = (ClampMin = "0.01", UIMin = "0.05"))
	float FallbackDashDuration = 0.2f;

	/** Fallback dash cooldown (seconds). */
	UPROPERTY(EditAnywhere, Config, Category = "Dash|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackDashCooldown = 0.75f;

	/** Fallback dash impulse speed (cm/s). */
	UPROPERTY(EditAnywhere, Config, Category = "Dash|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackDashSpeed = 1500.f;

	/** Fallback fraction of the dash duration that grants i-frames (0..1). */
	UPROPERTY(EditAnywhere, Config, Category = "Dash|Fallback", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FallbackDashIFrameFraction = 1.f;

	// ---- Fallback traversal traces ----

	/** Fallback forward trace distance (cm) used to look for a ledge/wall to mantle/vault. */
	UPROPERTY(EditAnywhere, Config, Category = "Traversal|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackLedgeReach = 80.f;

	/** Fallback maximum ledge height (cm) the character can mantle. */
	UPROPERTY(EditAnywhere, Config, Category = "Traversal|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackMaxMantleHeight = 200.f;

	/** Fallback minimum ledge height (cm) below which a low obstacle is vaulted instead of mantled. */
	UPROPERTY(EditAnywhere, Config, Category = "Traversal|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackVaultMaxHeight = 90.f;

	/** Fallback traversal interpolation duration (seconds) for mantle/vault. */
	UPROPERTY(EditAnywhere, Config, Category = "Traversal|Fallback", meta = (ClampMin = "0.01", UIMin = "0.05"))
	float FallbackTraversalDuration = 0.45f;

	// ---- Fallback slide / slope ----

	/** Fallback minimum floor angle (degrees) at which a slide accelerates downhill. */
	UPROPERTY(EditAnywhere, Config, Category = "Slide|Fallback", meta = (ClampMin = "0.0", ClampMax = "89.0"))
	float FallbackSlideSlopeThresholdDeg = 15.f;

	/** Fallback minimum speed (cm/s) below which a slide ends. */
	UPROPERTY(EditAnywhere, Config, Category = "Slide|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackSlideMinSpeed = 150.f;

	// ---- Fallback wall-run ----

	/** Fallback wall-run duration cap (seconds). */
	UPROPERTY(EditAnywhere, Config, Category = "WallRun|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackWallRunDuration = 1.5f;

	/** Fallback lateral wall-detection trace distance (cm). */
	UPROPERTY(EditAnywhere, Config, Category = "WallRun|Fallback", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float FallbackWallDetectDistance = 70.f;

	// ---- Air ----

	/** Fallback number of mid-air jumps allowed (1 = double jump; >1 = multi-jump). */
	UPROPERTY(EditAnywhere, Config, Category = "Air|Fallback", meta = (ClampMin = "0", UIMin = "0"))
	int32 FallbackAirJumpBudget = 1;

	// ---- Integration ----

	/** When true a UMove_StaminaComponent registers its ISeam_NeedProvider into the service locator. */
	UPROPERTY(EditAnywhere, Config, Category = "Integration")
	bool bRegisterStaminaAsService = true;
};
