// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Lvl_BiomeMaskComponent.generated.h"

class UCurveFloat;

/** One biome band: a tag and the noise-value window [Min,Max) that maps to it. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_BiomeBand
{
	GENERATED_BODY()

	/** Biome tag reported when the sampled noise value falls in this band. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Biome")
	FGameplayTag BiomeTag;

	/** Inclusive lower bound of the noise value (0..1) that maps to this biome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Biome",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MinValue = 0.0f;

	/** Exclusive upper bound of the noise value (0..1) that maps to this biome. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Biome",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MaxValue = 1.0f;

	FLvl_BiomeBand() = default;
};

/**
 * Per-machine, READ-ONLY helper that samples a biome (tag + 0..1 weight) at a world location.
 *
 * The biome field is data-authored: a deterministic value-noise function of the world XY (seeded by
 * NoiseSeed, scaled by NoiseFrequency) is optionally remapped through BiomeNoiseProfile, then bucketed
 * into one of the BiomeBands. The returned weight is how strongly the location sits inside its band
 * (1.0 at the band centre, falling toward the band edges) — useful for soft biome masking in scatter.
 *
 * NON-REPLICATED, NON-SAVED: a biome is a pure function of position + authored data, identical on every
 * machine, so it never needs to cross the wire. Used by ULvl_AdvancedPlacerComponent for biome gating.
 */
UCLASS(ClassGroup = "DesignPatterns|LevelDirector", meta = (BlueprintSpawnableComponent),
	HideCategories = ("ComponentReplication", "Cooking", "AssetUserData"))
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_BiomeMaskComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULvl_BiomeMaskComponent();

	/**
	 * Sample the biome at a world location. Returns the matching biome tag (invalid if no band matches)
	 * and fills OutWeight with the 0..1 membership weight (0 when no band matches).
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Biome")
	FGameplayTag GetBiomeAt(const FVector& WorldLoc, float& OutWeight) const;

	/** Raw deterministic noise value (0..1) at a world location, before banding (exposed for tools). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Biome")
	float SampleNoise01(const FVector& WorldLoc) const;

	// ---- Configuration --------------------------------------------------------------------------

	/** Ordered biome bands the sampled noise value is bucketed into. Empty -> no biome (invalid tag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Biome")
	TArray<FLvl_BiomeBand> BiomeBands;

	/**
	 * Optional remap curve applied to the raw 0..1 noise value before banding (lets a designer bias the
	 * biome distribution). Null -> identity (raw noise used directly).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Biome")
	TObjectPtr<UCurveFloat> BiomeNoiseProfile;

	/** Spatial frequency of the value noise (cycles per cm). Higher = smaller biome patches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Biome",
		meta = (ClampMin = "0.000001"))
	float NoiseFrequency = 0.0002f;

	/** Seed for the deterministic value noise, so the biome field is reproducible across machines/runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Biome")
	int32 NoiseSeed = 1337;

private:
	/** Deterministic, hash-based 2D value noise in [0,1] (bilinearly interpolated lattice). */
	float ValueNoise2D(float X, float Y) const;

	/** Hash a lattice corner (ix,iy) to a stable [0,1) value using NoiseSeed. */
	float LatticeHash(int32 IX, int32 IY) const;
};
