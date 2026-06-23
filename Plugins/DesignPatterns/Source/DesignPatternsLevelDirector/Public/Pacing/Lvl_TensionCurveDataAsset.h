// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Lvl_TensionCurveDataAsset.generated.h"

/**
 * DATA-ONLY pacing authoring consumed by ULvl_PacingDirectorSubsystem.
 *
 * A tension curve over NORMALIZED encounter time (0..1) is sampled into a 0..1 ProgressionInput that
 * drives the encounter director through ISeam_EncounterDirector. Because that director only accepts a
 * single ProgressionInput sampled at activation (it cannot mutate live intensity), the pacing director
 * re-activates only when tension crosses the escalate/relax thresholds authored here — so the curve
 * shape plus two thresholds fully specify a region's pacing.
 *
 * No magic numbers in code: the curve, thresholds, duration and ids all live here; the BudgetScalar a
 * richer director might want is intentionally absent — it is folded into ProgressionInput, the only
 * knob the AI director exposes.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_TensionCurveDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	ULvl_TensionCurveDataAsset();

	// ---- Identity -------------------------------------------------------------------------------

	/** Region this pacing curve applies to (matches the encounter activator's RegionTag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing|Identity")
	FGameplayTag RegionTag;

	/** Abstract encounter id handed to the encounter-director seam (adapter maps it to its asset). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing|Identity")
	FGameplayTag EncounterId;

	// ---- Tension curve --------------------------------------------------------------------------

	/**
	 * Tension over normalized encounter time. X in [0,1] (0 = encounter start, 1 = EncounterDuration),
	 * Y is tension (clamped to [0,1] at sample). Empty curve -> constant mid tension 0.5 (inert default).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing|Curve")
	FRuntimeFloatCurve TensionOverTime;

	/** Total wall-clock duration (s) the normalized curve maps onto. Clamped to a sane minimum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing|Curve",
		meta = (ClampMin = "0.1", ForceUnits = "s"))
	float EncounterDuration = 120.0f;

	/** If true, normalized time wraps (looping pacing); if false it clamps at 1.0 after EncounterDuration. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing|Curve")
	bool bLoop = false;

	// ---- Thresholds -----------------------------------------------------------------------------

	/**
	 * Tension at/above which the director re-activates the encounter at the escalated ProgressionInput.
	 * Must be > RelaxThreshold to form a hysteresis band (validated). Default a high-intensity bar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing|Thresholds",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EscalateThreshold = 0.66f;

	/**
	 * Tension at/below which the director relaxes (re-activates at a calmer ProgressionInput). Must be
	 * < EscalateThreshold; the gap is the hysteresis band that suppresses thrashing the AI director.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing|Thresholds",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RelaxThreshold = 0.33f;

	/** ProgressionInput (0..1) used while in the ESCALATED band. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing|Thresholds",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float EscalatedProgressionInput = 1.0f;

	/** ProgressionInput (0..1) used while in the RELAXED band. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Pacing|Thresholds",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RelaxedProgressionInput = 0.25f;

	// ---- Derived helpers ------------------------------------------------------------------------

	/** Sample tension (0..1) at a normalized time (0..1). Constant 0.5 when the curve is empty. */
	float SampleTension(float NormalizedTime01) const;

	/**
	 * Map a tension value to the ProgressionInput for whichever band it sits in. Above EscalateThreshold
	 * -> EscalatedProgressionInput; below RelaxThreshold -> RelaxedProgressionInput; in the hysteresis
	 * band -> a linear blend of the two (so a producer that does not use thresholds still gets a sane value).
	 */
	float TensionToProgressionInput(float Tension) const;

	/** Effective encounter duration (s), never below the safe minimum. */
	float GetEffectiveDuration() const;

	/** The effective escalate/relax thresholds with escalate strictly above relax (defensive clamp). */
	void GetClampedThresholds(float& OutRelax, float& OutEscalate) const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
