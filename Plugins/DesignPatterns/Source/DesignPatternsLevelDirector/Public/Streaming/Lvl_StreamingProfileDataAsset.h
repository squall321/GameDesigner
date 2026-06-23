// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Lvl_StreamingProfileDataAsset.generated.h"

/**
 * DATA-ONLY streaming/memory tuning consumed by ULvl_MemoryBudgetWatcherSubsystem (and read by
 * prefetch components to know which interest categories clamp first under pressure).
 *
 * No magic numbers in code: the watcher reads MemoryBudgetMB and the per-category priority weights
 * here; everything has a ClampMin and a validated accessor so a zero/negative budget can never drive
 * the pressure math. The profile does NOT command any streaming seam — it only publishes a per-machine
 * pressure scalar that prefetch components apply at the source (see the LevelDirector spec).
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_StreamingProfileDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	ULvl_StreamingProfileDataAsset();

	// ---- Memory budget --------------------------------------------------------------------------

	/**
	 * Soft memory budget (MB) for streamed content. When the resident set's estimated footprint
	 * exceeds this, the watcher raises pressure toward 1.0 proportional to the overshoot fraction.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Streaming|Budget",
		meta = (ClampMin = "1.0", ForceUnits = "MB"))
	float MemoryBudgetMB = 1024.0f;

	/**
	 * Overshoot fraction (over budget) at which pressure reaches its maximum (1.0). E.g. 0.25 means the
	 * watcher saturates pressure once usage is 25% over budget. Clamped to a sane minimum.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Streaming|Budget",
		meta = (ClampMin = "0.01", ClampMax = "10.0"))
	float PressureSaturationOvershoot = 0.25f;

	/** Seconds between budget evaluations (the watcher's FTSTicker cadence). Clamped to a minimum. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Streaming|Budget",
		meta = (ClampMin = "0.05", ForceUnits = "s"))
	float EvaluationIntervalSeconds = 0.5f;

	/**
	 * Estimated resident footprint (MB) attributed to one resident streaming level/cell when a precise
	 * platform query is unavailable. A defensive proxy: keeps the budget meaningful even on platforms
	 * where per-level memory accounting is not exposed. Clamped >= 0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Streaming|Budget",
		meta = (ClampMin = "0.0", ForceUnits = "MB"))
	float EstimatedMBPerResidentLevel = 32.0f;

	// ---- Category priority ----------------------------------------------------------------------

	/**
	 * Per-interest-category priority weight (higher = more important = clamped LAST under pressure).
	 * A prefetch component tagged with a low-priority category gets its extra radius clamped sooner
	 * when pressure rises. Categories absent from this map use DefaultCategoryPriority.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Streaming|Priority")
	TMap<FGameplayTag, float> CategoryPriority;

	/** Priority weight applied to a category not present in CategoryPriority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Streaming|Priority",
		meta = (ClampMin = "0.0"))
	float DefaultCategoryPriority = 1.0f;

	// ---- HLOD / LOD hints -----------------------------------------------------------------------

	/**
	 * Optional HLOD streaming-distance override (cm) a project may apply to far content under pressure.
	 * <= 0 means "no override" (engine defaults). Surfaced as data only — read by a project's HLOD
	 * wiring; this module never bakes an HLOD number into code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Streaming|HLOD",
		meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float HLODStreamingDistanceOverride = 0.0f;

	// ---- Derived helpers ------------------------------------------------------------------------

	/** Effective budget (MB), never <= 0 (defensive). */
	float GetEffectiveBudgetMB() const;

	/** Effective evaluation interval (s), never below the safe minimum. */
	float GetEffectiveEvaluationInterval() const;

	/** Priority weight for a category (map entry, else DefaultCategoryPriority, never negative). */
	float GetCategoryPriority(FGameplayTag Category) const;

	/** Effective per-resident MB proxy (>= 0). */
	float GetEstimatedMBPerResidentLevel() const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
