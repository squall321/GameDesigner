// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Combat_StatusEffect.generated.h"

class UCombat_StatusEffectComponent;
class AActor;

/**
 * A timed buff/debuff instance (DoT, stun, slow, etc.). GAS-FREE.
 *
 * Abstract + Blueprintable: project subclasses (C++ or BP) implement the designer hooks. An
 * instance is a UObject owned by a UCombat_StatusEffectComponent (its Outer), giving it a valid
 * world context and GC ownership via the component's active-effect array.
 *
 * Lifecycle (driven by the owning component, AUTHORITY side):
 *   OnApply -> (OnTick repeatedly every TickInterval) -> OnRemove
 * Each hook is a BlueprintNativeEvent so designers override behaviour without C++. Duration and
 * tick cadence are tracked by the component using world time.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, EditInlineNew, DefaultToInstanced)
class DESIGNPATTERNSCOMBAT_API UCombat_StatusEffect : public UObject
{
	GENERATED_BODY()

public:
	/** Identity tag of this effect (e.g. DP.Combat.Status.Burning). Mirrored into the component's
	 *  replicated active-effect tag container so clients know which effects are present. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status")
	FGameplayTag EffectTag;

	/** Total duration in seconds. <= 0 means infinite (until explicitly removed). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status", meta = (ClampMin = "0.0"))
	float Duration = 5.f;

	/** Seconds between OnTick calls. <= 0 disables ticking (apply/remove only). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "DesignPatternsCombat|Status", meta = (ClampMin = "0.0"))
	float TickInterval = 1.f;

	/** Magnitude designers interpret per-effect (damage per tick, slow %, etc.). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DesignPatternsCombat|Status")
	float Magnitude = 0.f;

	// ---- Designer-overridable lifecycle (BlueprintNativeEvent) ----

	/** Called once (authority) when the effect is applied to Target. Default logs only. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatternsCombat|Status")
	void OnApply(AActor* Target);
	virtual void OnApply_Implementation(AActor* Target);

	/** Called every TickInterval (authority) while active. Default logs only. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatternsCombat|Status")
	void OnTick(AActor* Target, float DeltaTime);
	virtual void OnTick_Implementation(AActor* Target, float DeltaTime);

	/** Called once (authority) when the effect expires or is removed. Default logs only. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatternsCombat|Status")
	void OnRemove(AActor* Target, bool bExpiredNaturally);
	virtual void OnRemove_Implementation(AActor* Target, bool bExpiredNaturally);

	//~ Begin UObject
	virtual UWorld* GetWorld() const override;
	//~ End UObject

	/** Resolve the owning component from the Outer chain. May be null in the CDO. */
	UCombat_StatusEffectComponent* GetOwningComponent() const;
};
