// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Volume.h"
#include "GameplayTagContainer.h"
#include "Seam/Lvl_SpawnRegionProvider.h"
#include "Lvl_SpawnRegionVolume.generated.h"

class ULvl_SpawnPointComponent;

/**
 * How a spawn-region volume produces its candidate spawn transforms.
 */
UENUM(BlueprintType)
enum class ELvl_SpawnRegionMode : uint8
{
	/** Use only the hand-authored ULvl_SpawnPointComponents under this actor. */
	ExplicitPointsOnly,

	/** Procedurally sample transforms inside the volume bounds (grid or jitter). */
	SampledOnly,

	/** Offer both explicit points and sampled points. */
	Both
};

/**
 * A team/tag-filtered spawn region. Implements ILvl_SpawnRegionProvider so the AI spawn director (a
 * separate module) can resolve it from the service locator and request matching spawn transforms,
 * without hard-depending on this class.
 *
 * Points come from two sources, selected by RegionMode: hand-authored ULvl_SpawnPointComponents
 * attached under the volume, and/or a deterministic procedural sampling of the volume's interior.
 * Every emitted point carries the region's RegionFilterTags (plus, for explicit points, the
 * component's own tags), so a filtered query only returns relevant transforms.
 *
 * This actor is purely local authoring data — it does NOT replicate and is NOT a persistence
 * participant. Spawning the actual actors is the spawn director's job; this volume only OFFERS
 * transforms. Sampling is deterministic from SamplingSeed so two machines (or a re-evaluation)
 * produce identical candidate sets when that matters.
 */
UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Lvl Spawn Region Volume"))
class DESIGNPATTERNSLEVELDIRECTOR_API ALvl_SpawnRegionVolume : public AVolume, public ILvl_SpawnRegionProvider
{
	GENERATED_BODY()

public:
	ALvl_SpawnRegionVolume();

	// ---- Authoring data -------------------------------------------------------------------------

	/** How this region produces candidate transforms (explicit points, sampled, or both). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn")
	ELvl_SpawnRegionMode RegionMode = ELvl_SpawnRegionMode::Both;

	/**
	 * Tags this whole region answers for (e.g. Team.Red). A query with filter F includes this region's
	 * points when F is invalid (match-all) or these tags contain F or a child. Explicit points may add
	 * their own finer tags on top.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn")
	FGameplayTagContainer RegionFilterTags;

	/** Yaw (degrees) applied to every emitted transform so spawned actors face a consistent direction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn", meta = (ClampMin = "-360.0", ClampMax = "360.0"))
	float FacingYaw = 0.f;

	// ---- Procedural sampling --------------------------------------------------------------------

	/** Number of procedurally sampled points to attempt (only in Sampled/Both modes). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn|Sampling", meta = (ClampMin = "0", EditCondition = "RegionMode != ELvl_SpawnRegionMode::ExplicitPointsOnly"))
	int32 SampleCount = 8;

	/** Minimum spacing (world units) enforced between sampled points (rejection sampling). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn|Sampling", meta = (ClampMin = "0.0", ForceUnits = "cm", EditCondition = "RegionMode != ELvl_SpawnRegionMode::ExplicitPointsOnly"))
	float MinSampleSpacing = 200.f;

	/**
	 * Deterministic seed for procedural sampling. Same seed + same volume => identical points, so a
	 * re-query (or another machine) reproduces the candidate set. Seed is data, never hardcoded logic.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn|Sampling")
	int32 SamplingSeed = 1337;

	/** Maximum rejection-sampling attempts per requested point before giving up (prevents infinite loops). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Spawn|Sampling", meta = (ClampMin = "1"))
	int32 MaxSampleAttemptsPerPoint = 16;

	// ---- ILvl_SpawnRegionProvider ---------------------------------------------------------------

	virtual void GetSpawnPoints_Implementation(FGameplayTag Filter, TArray<FTransform>& OutTransforms) const override;

	/** Gather every enabled, matching explicit spawn-point component under this actor. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Spawn")
	void GetExplicitPoints(FGameplayTag Filter, TArray<FTransform>& OutTransforms) const;

	/** Append deterministic sampled points inside the volume that pass the region filter. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Spawn")
	void GetSampledPoints(FGameplayTag Filter, TArray<FTransform>& OutTransforms) const;

protected:
	//~ Begin AActor
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor

	/** True if this region answers for the given filter (empty filter = always). */
	bool RegionMatchesFilter(FGameplayTag Filter) const;

	/** Build a spawn transform at Location with this region's facing yaw. */
	FTransform MakeSpawnTransform(const FVector& Location) const;

	/** True if a world location lies inside this volume's brush. */
	bool IsLocationInside(const FVector& WorldLocation) const;

private:
	/** Register this region under the shared spawn-region-provider service key. */
	void RegisterAsProvider();

	/** Remove this region's service registration. */
	void UnregisterAsProvider();
};
