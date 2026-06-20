// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "AI_WaveDataAsset.generated.h"

class AActor;

/**
 * One spawn entry inside a wave: WHAT to spawn, HOW MANY, where, how costly, and with what cadence.
 *
 * Pure data — a designer authors a row per enemy archetype in a wave. The actor class is a SOFT class
 * reference so a wave asset can be scanned/cooked without dragging gameplay code into memory; the spawn
 * director resolves it through the core factory at spawn time. No magic numbers live in code: count,
 * cost, delay and jitter are all authored here.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_SpawnEntry
{
	GENERATED_BODY()

	/**
	 * Factory identity tag for the thing to spawn. The director routes this through the core
	 * UDP_SpawnFactorySubsystem (which maps the tag to a recipe/factory), so the wave never names a
	 * concrete factory class. If a project prefers a direct class spawn, set ActorClassOverride instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave")
	FGameplayTag FactoryIdentityTag;

	/**
	 * OPTIONAL direct actor class to spawn when no factory identity is set (or no factory is registered
	 * for it). Soft so the asset stays scannable without loading gameplay classes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave", meta = (AllowAbstract = "false"))
	TSoftClassPtr<AActor> ActorClassOverride;

	/** How many of this entry to spawn (subject to the encounter's live budget cap). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave", meta = (ClampMin = "1", UIMin = "1", UIMax = "64"))
	int32 Count = 1;

	/**
	 * Budget cost charged per spawned instance of this entry. Mirrors the value an enemy reports via
	 * IAI_SpawnParticipant::GetBudgetCost; authoring it here lets the director plan a wave before any
	 * actor exists. Must be >= 1 so a wave cannot spawn infinitely under a finite budget.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave", meta = (ClampMin = "1", UIMin = "1", UIMax = "100"))
	int32 BudgetCost = 1;

	/**
	 * Spawn-region tag this entry spawns into. Resolved by an ILvl_SpawnRegionProvider when one is
	 * registered, otherwise by the director's fallback point list (matched on this same tag).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave")
	FGameplayTag SpawnRegionTag;

	/**
	 * Simulation-seconds to wait, from the start of the wave, before this entry begins spawning. Lets a
	 * wave stagger archetypes (e.g. melee first, ranged a few seconds later). Driven off the sim clock.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "60.0"))
	float StartDelaySeconds = 0.f;

	/**
	 * Simulation-seconds between successive spawns of THIS entry's instances (0 = all at once). Spreads a
	 * cluster out so a big count does not pop in on a single frame.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	float PerSpawnIntervalSeconds = 0.f;

	FAI_SpawnEntry() = default;
};

/**
 * A single wave: an ordered set of spawn entries plus its own pacing/budget knobs.
 *
 * A UDP_DataAsset so it is tag-identified (DataTag) and discoverable through the core data registry. An
 * encounter asset references a list of these by soft pointer; the spawn director plays them in order,
 * pacing each entry by the simulation clock and the encounter's live budget. Every tunable is authored
 * data — there are no hardcoded gameplay constants in the director for waves.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAI_API UAI_WaveDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Stable wave identity broadcast on DP.Bus.AI.Wave.* and attributed to spawned participants. Defaults
	 * to DataTag when left unset (so a designer only fills it to override the bus identity).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave")
	FGameplayTag WaveTag;

	/** The spawn entries that make up this wave, played in author order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave")
	TArray<FAI_SpawnEntry> Entries;

	/**
	 * Simulation-seconds to wait after the PREVIOUS wave is considered complete before this wave starts.
	 * The director's pacing reads this so designers control inter-wave breathing room without touching
	 * code. Only meaningful for waves after the first in an encounter.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "120.0"))
	float LeadInDelaySeconds = 0.f;

	/**
	 * When true the director waits until every budgeted participant from this wave is dead
	 * (DP.Bus.AI.Wave.Cleared) before advancing to the next wave; when false it advances as soon as the
	 * wave finished SPAWNING (DP.Bus.AI.Wave.Completed). Lets designers pick "clear the room" vs. "endless
	 * pressure" pacing per wave.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave")
	bool bRequireClearBeforeNext = true;

	/**
	 * Optional per-wave additional budget granted when the wave starts (on top of the encounter base
	 * budget). 0 means the wave draws purely from the encounter budget.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Wave", meta = (ClampMin = "0", UIMin = "0", UIMax = "500"))
	int32 BonusBudget = 0;

	/** Sum of (Count * BudgetCost) across all entries — the worst-case budget this wave wants. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Wave")
	int32 GetPlannedBudgetCost() const;

	/** Total planned participant count across all entries. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Wave")
	int32 GetPlannedCount() const;

	/** Effective wave tag (WaveTag if set, else DataTag). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Wave")
	FGameplayTag GetEffectiveWaveTag() const;

	//~ Begin UDP_DataAsset
	/** Groups all wave assets into one asset-manager bucket. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags empty entry lists and entries with neither a factory tag nor an actor class. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
