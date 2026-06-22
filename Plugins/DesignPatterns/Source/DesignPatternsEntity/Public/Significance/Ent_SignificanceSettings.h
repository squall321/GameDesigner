// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Data/DPDataAsset.h"
#include "Ent_SignificanceSettings.generated.h"

/** Discrete significance bucket an entity falls into, coarsest to finest detail. */
UENUM(BlueprintType)
enum class EEnt_SignificanceBucket : uint8
{
	/** Beyond the last band — culled / lowest detail / slowest tick. */
	Culled,
	/** Far band. */
	Low,
	/** Mid band. */
	Medium,
	/** Near band — highest detail / fastest tick. */
	High
};

/**
 * One authored significance band. Entities within MaxDistance of the significance source (and within the
 * per-band count budget) fall into this band, which dictates their tick interval and a detail level the
 * owner can interpret. All numbers are authored — the manager hardcodes nothing.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_SignificanceBucketDef
{
	GENERATED_BODY()

	/** The bucket this band assigns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Significance")
	EEnt_SignificanceBucket Bucket = EEnt_SignificanceBucket::High;

	/** Maximum distance (world units) from the significance source for an entity to be in this band. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Significance", meta = (ClampMin = "0"))
	float MaxDistance = 1000.f;

	/**
	 * Maximum number of entities allowed in this band; overflow (by ascending distance) spills into the
	 * next-coarser band. <= 0 means unbounded.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Significance")
	int32 CountBudget = 0;

	/** Tick interval (seconds) the manager pushes onto entities in this band (0 = every frame). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Significance", meta = (ClampMin = "0"))
	float TickInterval = 0.f;

	/** Opaque detail level the owner interprets (e.g. mesh LOD bias, AI fidelity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Entity|Significance")
	int32 DetailLevel = 0;
};

/**
 * Authored LOD/significance tuning consumed by UEnt_SignificanceManagerSubsystem.
 *
 * Bands are ordered finest-first (High near, Culled far). The manager scores each entity by distance to
 * the significance source, places it in the first band whose MaxDistance it is within (respecting count
 * budgets), and pushes that band's tick interval + detail level onto the entity's significance component.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Entity Significance Settings"))
class DESIGNPATTERNSENTITY_API UEnt_SignificanceSettings : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Significance bands, authored finest-first. Empty = everything stays High (no budgeting). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Entity|Significance")
	TArray<FEnt_SignificanceBucketDef> Buckets;

	//~ Begin UDP_DataAsset.
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset.
};
