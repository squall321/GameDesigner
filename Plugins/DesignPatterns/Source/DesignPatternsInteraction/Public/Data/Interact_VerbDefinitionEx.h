// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/Interact_VerbDefinition.h"
#include "Types/Interact_AvailabilityTypes.h"
#include "Interact_VerbDefinitionEx.generated.h"

/**
 * Extended verb definition adding richer activation styles (tap / hold / charge / double-tap /
 * repeat) as DATA, without touching the shipped UInteract_VerbDefinition.
 *
 * COMPATIBILITY
 *   - The shipped bHoldToActivate / HoldSeconds remain the canonical authority for Hold timing, so
 *     UInteract_InteractorComponent::GetActiveHoldProgress keeps working unchanged. When the chosen
 *     ActivationMode is Hold (or Charge) the constructor-driven contract is: set bHoldToActivate=true
 *     and use HoldSeconds — the input driver derives the LOCAL gesture only; the server's
 *     hold-completion still measures HoldSeconds against the replicated start time.
 *   - A plain (base-class) verb is treated as Tap (instant) or Hold by the input driver depending on
 *     bHoldToActivate, so this subclass is purely additive.
 *
 * Like its base it does NOT override GetDataAssetType(), forming its own asset-manager bucket.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Interact Verb Definition (Extended)"))
class DESIGNPATTERNSINTERACTION_API UInteract_VerbDefinitionEx : public UInteract_VerbDefinition
{
	GENERATED_BODY()

public:
	UInteract_VerbDefinitionEx();

	/**
	 * How this verb is activated by player input. The input driver reads this to shape the LOCAL
	 * gesture; the authoritative completion of a Hold/Charge is still measured server-side from the
	 * replicated start time against the base-class HoldSeconds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|Verb|Activation")
	EInteract_ActivationMode ActivationMode = EInteract_ActivationMode::Tap;

	/**
	 * Maximum time (seconds) a press is allowed before a second press counts as a separate tap, for
	 * DoubleTap activation. A second press within this window activates the verb.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|Verb|Activation",
		meta = (EditCondition = "ActivationMode == EInteract_ActivationMode::DoubleTap", ClampMin = "0.0", Units = "s"))
	float DoubleTapWindowSeconds = 0.3f;

	/**
	 * For Charge activation, how long (seconds) holding the input takes the charge from 0 to 1.
	 * Releasing fires the verb with the accumulated [0,1] charge level (see GetLocalChargeAlpha).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|Verb|Activation",
		meta = (EditCondition = "ActivationMode == EInteract_ActivationMode::Charge", ClampMin = "0.0", Units = "s"))
	float ChargeMaxSeconds = 1.0f;

	/**
	 * For Repeat activation, the interval (seconds) between successive auto-fires while held.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Interact|Verb|Activation",
		meta = (EditCondition = "ActivationMode == EInteract_ActivationMode::Repeat", ClampMin = "0.01", Units = "s"))
	float RepeatIntervalSeconds = 0.25f;

	/**
	 * Resolve the effective activation mode of any verb definition: returns this subclass's
	 * ActivationMode when it is an Ex definition, otherwise derives Tap/Hold from the base class's
	 * bHoldToActivate. Static so callers can pass a base-class pointer without a cast.
	 */
	static EInteract_ActivationMode ResolveActivationMode(const UInteract_VerbDefinition* Def);
};
