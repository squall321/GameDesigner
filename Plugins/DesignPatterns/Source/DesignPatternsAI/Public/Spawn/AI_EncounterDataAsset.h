// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "AI_EncounterDataAsset.generated.h"

class UAI_WaveDataAsset;

/**
 * Gate condition checked against the World hub before an encounter is allowed to activate.
 *
 * The director reads the named hub flag (IWorldHub_Queryable::QueryValue at Global scope) and requires
 * it to equal bExpectedValue. All conditions must pass (AND) for activation. Pure data so designers
 * gate "the boss arena door is open" etc. without code.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_EncounterGateCondition
{
	GENERATED_BODY()

	/** World-hub boolean flag key to test (Global scope). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter")
	FGameplayTag HubFlagKey;

	/** The value the flag must hold for this condition to pass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter")
	bool bExpectedValue = true;

	FAI_EncounterGateCondition() = default;
};

/**
 * A full encounter: an ordered list of waves plus budget/difficulty/pacing curves and hub gates.
 *
 * A UDP_DataAsset (tag-identified, registry-discoverable). The spawn director consumes exactly one of
 * these at a time: it gates activation on the hub conditions, then plays the waves in order, sampling
 * the budget and difficulty curves against an external progression input (e.g. day number, player count,
 * or clear count) supplied by the project. NO magic numbers — budget, difficulty and pacing are curves
 * and tunables authored here; the director only reads them.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAI_API UAI_EncounterDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Stable encounter identity broadcast on DP.Bus.AI.Encounter.* and stamped on its waves. Defaults to
	 * DataTag when unset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter")
	FGameplayTag EncounterTag;

	/** The waves to play, in order. Soft so the encounter asset stays scannable without loading waves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter")
	TArray<TSoftObjectPtr<UAI_WaveDataAsset>> Waves;

	/**
	 * Conditions (all must pass) checked against the World hub before the encounter activates. Empty =
	 * always allowed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter")
	TArray<FAI_EncounterGateCondition> GateConditions;

	/**
	 * Base concurrent budget for the encounter before the difficulty curve scales it. The live budget is
	 * Round(BaseBudget * difficulty) + per-wave bonuses. Authored, never hardcoded.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter|Budget", meta = (ClampMin = "1", UIMin = "1", UIMax = "1000"))
	int32 BaseBudget = 20;

	/**
	 * Budget multiplier as a function of the project-supplied progression input X (e.g. day number or
	 * clear count). Sampled once at activation. A flat curve (constant 1) means budget never scales.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter|Budget")
	FRuntimeFloatCurve BudgetByProgression;

	/**
	 * Difficulty scalar as a function of the progression input X. Broadcast in the encounter payload and
	 * available to the project to scale enemy stats. Sampled once at activation.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter|Difficulty")
	FRuntimeFloatCurve DifficultyByProgression;

	/**
	 * Global pacing multiplier applied to every wave's lead-in/start delays (1 = author values verbatim,
	 * <1 = tighter pacing, >1 = slower). Lets a project speed up or slow down a whole encounter without
	 * re-authoring every wave.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter|Pacing", meta = (ClampMin = "0.05", ClampMax = "10.0", UIMin = "0.25", UIMax = "4.0"))
	float PacingTimeScale = 1.f;

	/**
	 * When true the encounter loops back to its first wave after the last completes (endless mode);
	 * when false it fires DP.Bus.AI.Encounter.Completed and stops.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Encounter|Pacing")
	bool bLooping = false;

	// ---- Curve sampling helpers (clamped, with documented fallbacks) ----

	/**
	 * Sample the live budget for a progression input. @return Round(BaseBudget * BudgetByProgression(X)),
	 * clamped to at least 1. If the curve has no keys the multiplier defaults to 1 (BaseBudget verbatim).
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Encounter")
	int32 SampleBudget(float ProgressionInput) const;

	/**
	 * Sample the difficulty scalar for a progression input. @return DifficultyByProgression(X), clamped
	 * to >= 0. If the curve has no keys it defaults to 1.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Encounter")
	float SampleDifficulty(float ProgressionInput) const;

	/** Effective encounter tag (EncounterTag if set, else DataTag). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Encounter")
	FGameplayTag GetEffectiveEncounterTag() const;

	/** Number of authored wave slots (including any unset/null soft pointers). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Encounter")
	int32 GetWaveCount() const { return Waves.Num(); }

	//~ Begin UDP_DataAsset
	/** Groups all encounter assets into one asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags an empty wave list and gate conditions with no flag key. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
