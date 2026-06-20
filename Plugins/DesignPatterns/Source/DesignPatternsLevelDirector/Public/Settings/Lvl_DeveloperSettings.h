// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Lvl_DeveloperSettings.generated.h"

class UDataAsset;

/**
 * One distance band in the streaming policy. Bands are evaluated nearest-first: a streaming level
 * whose distance to the closest interest source falls within [0, LoadWithinDistance] should be
 * requested resident; one beyond UnloadBeyondDistance should be requested unloaded. The gap between
 * the two (Load < Unload) is the HYSTERESIS region — levels there keep their current state, which
 * prevents thrashing a level that sits right on a threshold.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_DistanceBand
{
	GENERATED_BODY()

	/** Designer-facing label for this band (e.g. "Near", "Mid", "Far"). Diagnostic only. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming")
	FName BandName = NAME_None;

	/**
	 * Optional category tag this band applies to. A streaming level/cell tagged with this (or a child)
	 * uses this band's distances; if invalid the band is the DEFAULT band applied to untagged levels.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming")
	FGameplayTag AppliesToCategory;

	/** Load a level resident when an interest source is within this distance (world units). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float LoadWithinDistance = 12000.f;

	/**
	 * Unload a level when the NEAREST interest source is beyond this distance (world units). Must be
	 * >= LoadWithinDistance; the difference is the hysteresis band that suppresses load/unload churn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float UnloadBeyondDistance = 18000.f;

	/**
	 * Whether levels in this band should be made VISIBLE once loaded, or merely loaded-but-hidden
	 * (pre-warmed). Pre-warming lets a far band pay the load cost early and only pay the make-visible
	 * cost when promoted to a nearer, visible band.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Streaming")
	bool bMakeVisibleWhenLoaded = true;
};

/**
 * Project-wide configuration for the level-director streaming system. Appears under
 * Project Settings -> Plugins -> Design Patterns Level Director. All gameplay-tunable numbers
 * (distance bands, per-frame budgets, evaluation cadence, default policy asset) live here as data;
 * no magic numbers are baked into the director code beyond documented defensive fallbacks used only
 * when this settings CDO cannot be resolved.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Level Director"))
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	ULvl_DeveloperSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience accessor. May return nullptr in extremely early load; callers fall back defensively. */
	static const ULvl_DeveloperSettings* Get();

	// ---- Distance-band policy ------------------------------------------------------------------

	/**
	 * Ordered distance bands the director applies to streaming levels/cells. Authored here as the
	 * project default; a game may also point DefaultPolicyAsset at a data asset that supplies bands
	 * per level set. When this array is empty AND no policy asset resolves, the director uses a single
	 * built-in fallback band (see DefaultFallbackLoad/UnloadDistance) so streaming still functions.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Policy")
	TArray<FLvl_DistanceBand> DistanceBands;

	/**
	 * Optional data asset supplying additional/override streaming policy (per level-set bands, named
	 * profiles). Soft so it need not be loaded to read the simple inline bands above. The director
	 * resolves it lazily and degrades gracefully (uses DistanceBands) if it fails to load.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Policy", meta = (AllowedClasses = "/Script/Engine.DataAsset"))
	TSoftObjectPtr<UDataAsset> DefaultPolicyAsset;

	/** Fallback load distance used ONLY when no bands and no policy asset are available (world units). */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Policy", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float DefaultFallbackLoadDistance = 12000.f;

	/** Fallback unload distance used ONLY when no bands and no policy asset are available (world units). */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Policy", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float DefaultFallbackUnloadDistance = 18000.f;

	// ---- Per-frame budgets ---------------------------------------------------------------------

	/**
	 * Maximum number of level/cell LOAD requests the director may issue in a single evaluation pass.
	 * Caps the hitch from many levels entering range at once. 0 means "no cap" (issue all at once).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Budget", meta = (ClampMin = "0"))
	int32 MaxLoadRequestsPerFrame = 2;

	/** Maximum number of level/cell UNLOAD requests the director may issue per evaluation pass. 0 = no cap. */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Budget", meta = (ClampMin = "0"))
	int32 MaxUnloadRequestsPerFrame = 4;

	/**
	 * Maximum number of streaming levels evaluated per pass. Large World-Partition or many-sublevel
	 * worlds amortize evaluation across frames in a round-robin cursor. 0 = evaluate all every pass.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Budget", meta = (ClampMin = "0"))
	int32 MaxLevelsEvaluatedPerFrame = 64;

	// ---- Evaluation cadence --------------------------------------------------------------------

	/**
	 * Seconds between director evaluation passes. The director does not run every tick; streaming
	 * decisions tolerate latency and a coarse cadence saves CPU. Clamped to a sane minimum.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Cadence", meta = (ClampMin = "0.0", ForceUnits = "s"))
	float EvaluationIntervalSeconds = 0.25f;

	/**
	 * Minimum distance an interest source must move (world units) before a re-evaluation is forced
	 * ahead of the cadence timer. 0 disables move-triggered re-evaluation (cadence only).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Cadence", meta = (ClampMin = "0.0", ForceUnits = "cm"))
	float MoveReevaluateThreshold = 500.f;

	// ---- World Partition -----------------------------------------------------------------------

	/**
	 * When true the director will, if a UWorldPartitionSubsystem is present, register a streaming
	 * source per interest source so WP streams cells around them. When false (or WP absent) the
	 * director manages classic ULevelStreaming sublevels only. Resolved softly at runtime.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|World Partition")
	bool bDriveWorldPartitionSources = true;

	/** Priority assigned to director-registered World Partition streaming sources (higher = sooner). */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|World Partition", meta = (ClampMin = "0"))
	int32 WorldPartitionSourcePriority = 100;

	// ---- Analytics -----------------------------------------------------------------------------

	/** When true, the director emits an aggregate streaming-churn analytics event each interval. */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Analytics")
	bool bEmitChurnAnalytics = true;

	/** Seconds between aggregate churn analytics events (rate limit). Clamped to a sane minimum. */
	UPROPERTY(EditAnywhere, Config, Category = "Streaming|Analytics", meta = (ClampMin = "1.0", ForceUnits = "s"))
	float AnalyticsReportIntervalSeconds = 30.f;
};
