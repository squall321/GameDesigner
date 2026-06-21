// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "Move_StaminaProfile.generated.h"

/**
 * Authored-once stamina tuning asset. The UMove_StaminaComponent reads its numbers from one of these so
 * there are no magic costs/rates in the component. A field left <= 0 falls back to the project's
 * Move_DeveloperSettings (documented defensive default). Multiple stamina components may share one asset.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSMOVEMENT_API UMove_StaminaProfile : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Maximum stamina. <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float MaxStamina = 0.f;

	/** Stamina regenerated per second while not draining and past the regen delay. <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float RegenPerSecond = 0.f;

	/** Stamina drained per second while sprinting. <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float SprintDrainPerSecond = 0.f;

	/** Flat stamina cost of a dash/dodge. <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float DashCost = 0.f;

	/** Flat stamina cost of a single jump. 0 -> jumps are free. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float JumpCost = 0.f;

	/** Delay (seconds) after the last drain before regen resumes. <= 0 -> settings fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float RegenDelay = 0.f;

	/**
	 * When stamina hits zero, the minimum stamina the character must regenerate back to before sprint/
	 * dash are permitted again (anti-flutter "exhausted" gate). 0 disables the gate. Expressed as an
	 * absolute stamina value (clamped to MaxStamina at read time).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Stamina", meta = (ClampMin = "0.0"))
	float ExhaustionRecoveryThreshold = 0.f;
};
