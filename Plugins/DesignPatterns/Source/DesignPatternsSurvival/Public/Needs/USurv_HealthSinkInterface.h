// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "USurv_HealthSinkInterface.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USurv_HealthSinkInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Soft seam to a health/damage system (e.g. a Combat module) WITHOUT a hard dependency.
 *
 * The needs component calls ApplyNeedsDamage when a critical need would harm the actor. Any
 * actor or component that implements this interface receives the damage; if nothing implements
 * it, starvation/dehydration is simply cosmetic. This keeps DesignPatternsSurvival decoupled
 * from any specific combat module — the Combat module (if present) implements the interface, or
 * the project binds USurv_NeedsComponent::OnNeedsDamage instead.
 */
class ISurv_HealthSinkInterface
{
	GENERATED_BODY()

public:
	/**
	 * Apply DamageAmount caused by a critical survival need (SourceNeedTag identifies which one).
	 * Return the damage actually applied (for logging/feedback). Authority-only by contract.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Survival|Needs")
	float ApplyNeedsDamage(float DamageAmount, FGameplayTag SourceNeedTag);
};
