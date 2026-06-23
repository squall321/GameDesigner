// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Engine/EngineTypes.h"
#include "Lvl_ScatterModifierDataAsset.generated.h"

/**
 * DATA-ONLY tuning for ULvl_AdvancedPlacerComponent's Bridson Poisson-disk + distance-field + biome
 * scatter. Mirrors ULvl_PlacementRuleSet's discipline: every tunable is EditAnywhere with ClampMin so
 * no zero/negative reaches the generator math; IsDataValid flags a degenerate configuration.
 *
 * No behaviour — the advanced placer reads these values and produces an FLvl_PlacementManifest that it
 * hands to a co-located ULvl_ProceduralPlacerComponent via the existing public RestoreFromManifest, so
 * spawning still flows through the core factory + pool.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_ScatterModifierDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	ULvl_ScatterModifierDataAsset();

	// ---- Poisson-disk (Bridson) -----------------------------------------------------------------

	/** Minimum distance (cm) between any two accepted scatter points (Poisson-disk radius). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Poisson",
		meta = (ClampMin = "1.0", ForceUnits = "cm"))
	float PoissonMinRadius = 300.0f;

	/** Bridson "k": candidate samples tried around each active point before it is retired. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Poisson",
		meta = (ClampMin = "1"))
	int32 PoissonMaxAttempts = 30;

	/** Hard cap on accepted Poisson points per pass (safety bound against a huge area). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Poisson",
		meta = (ClampMin = "1"))
	int32 MaxScatterPoints = 1024;

	/** Local half-extent (cm) of the XY sampling area, centred on the owner. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Poisson",
		meta = (ClampMin = "1.0", ForceUnits = "cm"))
	FVector2D SampleAreaExtent = FVector2D(2000.0, 2000.0);

	// ---- Distance field -------------------------------------------------------------------------

	/**
	 * Distance-field falloff over normalized distance (0 at the area centre .. 1 at the edge) mapped to
	 * an acceptance weight (0..1). A candidate is accepted only when a deterministic roll < the curve
	 * value at its normalized distance. Empty curve -> uniform acceptance (documented inert default).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|DistanceField")
	FRuntimeFloatCurve DistanceFieldFalloff;

	/** Multiplier on the distance-field acceptance weight (lets a designer thin/densify globally). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|DistanceField",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DensityThreshold = 1.0f;

	// ---- Biome mask -----------------------------------------------------------------------------

	/**
	 * If non-empty, a candidate's sampled biome tag (from ULvl_BiomeMaskComponent) must match (be a
	 * child of) one of these to be accepted. Empty -> no biome gating.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Biome")
	FGameplayTagContainer AllowedBiomeTags;

	/** Minimum biome weight (0..1) a candidate's cell must report to be accepted (0 -> any weight). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Biome",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinBiomeWeight = 0.0f;

	// ---- Class table ----------------------------------------------------------------------------

	/**
	 * Weighted actor-class identity tags to scatter. Each accepted point picks one (deterministically);
	 * the chosen tag is recorded into the manifest entry and resolved through the core factory at spawn.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Classes")
	TArray<FGameplayTag> ScatterClassTags;

	// ---- Surface projection ---------------------------------------------------------------------

	/** If true, each accepted point is projected onto the world surface by a downward trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Surface")
	bool bProjectToSurface = true;

	/** Trace channel for the surface projection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Surface",
		meta = (EditCondition = "bProjectToSurface"))
	TEnumAsByte<ECollisionChannel> SurfaceTraceChannel = ECC_WorldStatic;

	/** Height (cm) above the candidate the downward trace starts from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Surface",
		meta = (EditCondition = "bProjectToSurface", ClampMin = "0.0", ForceUnits = "cm"))
	float TraceStartHeight = 5000.0f;

	/** Total downward distance (cm) the projection trace travels. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Scatter|Surface",
		meta = (EditCondition = "bProjectToSurface", ClampMin = "1.0", ForceUnits = "cm"))
	float TraceDistance = 20000.0f;

	// ---- Derived helpers ------------------------------------------------------------------------

	/** Sample the distance-field acceptance weight (0..1) at a normalized distance; 1 when no curve. */
	float SampleDistanceFieldWeight(float NormalizedDistance01) const;

	/** True if there is at least one usable scatter class tag. */
	bool HasUsableClass() const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
