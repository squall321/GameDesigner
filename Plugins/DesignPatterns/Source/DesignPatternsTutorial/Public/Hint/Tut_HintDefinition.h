// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"

#include "Tut_HintDefinition.generated.h"

class UTut_Condition;

/**
 * A single contextual hint — a tag-identified UDP_DataAsset resolved through the core data registry.
 *
 * A hint surfaces (as a HUD toast) when its Condition is satisfied, subject to per-hint cooldown, a global
 * cooldown, and priority arbitration among competing eligible hints. Like a tutorial, a hint holds only data;
 * UTut_HintSubsystem owns the evaluation, cooldown, queue and surfacing behaviour.
 *
 * Trigger bus channels (TriggerEventTags) are an optimisation hint to the subsystem: a hint whose condition is
 * bus-event-driven lists the channels it cares about so the subsystem can evaluate it only when one of those
 * events arrives, instead of every evaluation tick. A hint with no trigger channels is evaluated on the
 * periodic cadence (for purely hub-state-driven hints).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Hint Definition"))
class DESIGNPATTERNSTUTORIAL_API UTut_HintDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * The condition that, when satisfied, makes this hint eligible to surface. Composed inline (EditInlineNew)
	 * so designers author hint triggers with no code. When null the hint is never eligible (must be triggered
	 * by a project explicitly).
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Hint")
	TObjectPtr<UTut_Condition> Condition;

	/** The hint text surfaced to the player (as a HUD toast). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (MultiLine = true))
	FText Text;

	/**
	 * Minimum seconds before this same hint may surface again after it last fired. 0 means it may re-fire as
	 * soon as it becomes eligible again (still subject to the global cooldown). Tunable per hint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (ClampMin = "0.0"))
	float CooldownSeconds = 30.f;

	/**
	 * Relative importance among competing eligible hints. When several hints are eligible at once, the
	 * highest-priority one surfaces first. Ties break by registration order. Tunable per hint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint")
	int32 Priority = 0;

	/**
	 * On-screen duration (seconds) for this hint's toast. <= 0 uses the developer-settings default. Passed
	 * through to the HUD notification request.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (ClampMin = "0.0"))
	float DisplaySeconds = 0.f;

	/**
	 * The maximum number of times this hint may ever surface in a session. 0 = unlimited. A one-shot
	 * "first-time" hint sets this to 1. Tunable per hint.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (ClampMin = "0"))
	int32 MaxShowCount = 0;

	/**
	 * The HUD notification category the toast is styled with (child of DP.HUD.Notify). Empty uses the
	 * module's default hint category. Opaque tag — no styling logic lives here.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (Categories = "DP.HUD.Notify"))
	FGameplayTag NotificationCategory;

	/**
	 * Bus channels this hint's condition reacts to (optimisation: the subsystem evaluates the hint when one of
	 * these arrives). Empty = evaluate on the periodic cadence only. Each should match a channel the hint's
	 * UTut_Condition_BusEvent keys on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hint", meta = (Categories = "DP.Bus"))
	FGameplayTagContainer TriggerEventTags;

	//~ Begin UDP_DataAsset
	/** Collapses every hint definition into one shared asset-manager type bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Warns on hints with no text or no condition. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
