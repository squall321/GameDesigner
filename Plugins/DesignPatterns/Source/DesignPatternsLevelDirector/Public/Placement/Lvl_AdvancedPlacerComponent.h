// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Placement/Lvl_PlacementTypes.h"
#include "Lvl_AdvancedPlacerComponent.generated.h"

class ULvl_ScatterModifierDataAsset;
class ULvl_BiomeMaskComponent;
class ULvl_ProceduralPlacerComponent;
struct FHitResult;

/**
 * STANDALONE additive scatter generator — deeper than the base ULvl_ProceduralPlacerComponent's
 * jittered-grid/disc scatter, but NOT a subclass (the base's candidate/validation pipeline is private
 * and cannot be overridden additively).
 *
 * It builds candidates with a Bridson Poisson-disk distribution, gates them through a distance-field
 * falloff and an optional biome mask (ULvl_BiomeMaskComponent), projects accepted points onto the world
 * surface, assembles an FLvl_PlacementManifest, and hands it to a co-located ULvl_ProceduralPlacerComponent
 * via the EXISTING public RestoreFromManifest. So spawning, pooling, save, and the placement bus event
 * all flow through the proven base path — this component only owns the smarter candidate generation.
 *
 * Determinism: a single FRandomStream(GetEffectiveSeed()) drives the whole pass, so the scatter is
 * reproducible across runs and across save/load (the base placer stores the seed in the manifest).
 *
 * AUTHORITY: GenerateScatter is authority-guarded at the TOP (no-op on clients); the actors it asks the
 * base placer to spawn replicate themselves normally.
 */
UCLASS(ClassGroup = "DesignPatterns|LevelDirector", meta = (BlueprintSpawnableComponent),
	HideCategories = ("ComponentReplication", "Cooking", "AssetUserData"))
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_AdvancedPlacerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULvl_AdvancedPlacerComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	//~ End UActorComponent

	/** True on server / standalone / listen-server host. ALL placement gates on this at the TOP. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Scatter")
	bool HasWorldAuthority() const;

	// ---- Configuration --------------------------------------------------------------------------

	/** The scatter tuning this component executes. Required for a pass to run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Scatter")
	TObjectPtr<ULvl_ScatterModifierDataAsset> ScatterModifier;

	/** Optional biome source. When set, candidates are biome-gated per the modifier's AllowedBiomeTags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Scatter")
	TObjectPtr<ULvl_BiomeMaskComponent> BiomeMaskSource;

	/**
	 * The base placer this component drives. When null, BeginPlay resolves a sibling
	 * ULvl_ProceduralPlacerComponent on the owner (composition, not inheritance).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Scatter")
	TObjectPtr<ULvl_ProceduralPlacerComponent> TargetPlacer;

	/** Logical region tag stamped into the produced manifest (routes save/restore back here). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Scatter")
	FGameplayTag RegionTag;

	/** Optional seed override. >= 0 replaces the modifier-derived seed for this component. -1 = derive. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Scatter")
	int32 SeedOverride = -1;

	/** If true, GenerateScatter runs automatically on BeginPlay (authority only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Scatter")
	bool bGenerateOnBeginPlay = false;

	// ---- Pass control (AUTHORITY ONLY) ----------------------------------------------------------

	/**
	 * Run the advanced scatter pass: build the manifest and feed it to the target placer via
	 * RestoreFromManifest. Authority-guarded at the top. @return true if at least one actor was placed.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Scatter")
	bool GenerateScatter();

	/** Effective seed (override if >= 0, else derived from the owner name hash — same policy as the placer). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Scatter")
	int32 GetEffectiveSeed() const;

	/** Resolve the target placer (explicit, else a sibling on the owner). May be null. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Scatter")
	ULvl_ProceduralPlacerComponent* ResolveTargetPlacer() const;

private:
	// ---- Generation internals -------------------------------------------------------------------

	/**
	 * Bridson Poisson-disk sampling over the modifier's XY SampleAreaExtent (owner-local), producing
	 * world-space candidate points spaced at least PoissonMinRadius apart. Deterministic via the stream.
	 */
	void GeneratePoissonCandidates(FRandomStream& Stream, TArray<FVector>& OutCandidates) const;

	/**
	 * Distance-field + biome acceptance test for one world candidate. Uses a deterministic roll from the
	 * stream against the distance-field falloff, and the biome mask when present.
	 */
	bool PassesBiomeAndDistanceField(FRandomStream& Stream, const FVector& WorldLoc) const;

	/** Project a candidate onto the world surface (downward trace) per the modifier; returns final loc. */
	bool ProjectToSurface(const FVector& Candidate, FVector& OutLocation) const;

	/** Build a deterministic placement id from the seed + candidate index (mirrors the base placer). */
	static FGuid MakeDeterministicScatterId(int32 Seed, int32 CandidateIndex);
};
