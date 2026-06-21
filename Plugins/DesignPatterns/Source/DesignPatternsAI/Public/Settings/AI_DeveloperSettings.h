// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "AI_DeveloperSettings.generated.h"

/**
 * Project-wide configuration for the DesignPatternsAI module. Appears under
 * Project Settings → Plugins → Design Patterns AI. Editing here requires no code.
 *
 * These are the genre-neutral tunables for the squad + spawn-director systems: default encounter
 * budget/cap when an asset does not override it, director pacing cadence, squad sizing, and the
 * fallback formation spacing used when no formation data is supplied. The spawn director and squad
 * subsystem read these from the CDO; when the CDO is somehow null they use the documented inline
 * defensive defaults baked next to each consumer. There are no hardcoded magic gameplay numbers in
 * the subsystem logic — everything tunable lives here.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns AI"))
class DESIGNPATTERNSAI_API UAI_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAI_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Group under the "Plugins" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	//~ End UDeveloperSettings

	// ---- Spawn director: budget ----

	/**
	 * Default concurrent live-budget the director uses when an encounter asset is absent or its
	 * BaseBudget resolves to zero. This is the safety-net cap, not the per-encounter value (that comes
	 * from the asset's budget curve). Authored, never hardcoded in the director.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Spawn|Budget", meta = (ClampMin = "1", ClampMax = "1000", UIMin = "1", UIMax = "200"))
	int32 DefaultEncounterBudget = 20;

	/**
	 * Absolute ceiling on the number of director-spawned participants alive at once, regardless of
	 * budget. Protects framerate when an encounter's costs are mis-authored. The director never exceeds
	 * this even if budget would allow more.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Spawn|Budget", meta = (ClampMin = "1", ClampMax = "2000", UIMin = "1", UIMax = "300"))
	int32 SpawnCap = 100;

	// ---- Spawn director: pacing ----

	/**
	 * How many times per second the director re-evaluates pacing (advancing entry delays, checking wave
	 * completion/clear, reconciling the live budget). This is a coordination cadence, NOT a spawn rate;
	 * keeping it low keeps the director cheap. The actual spawn times come from the wave/entry data.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Spawn|Pacing", meta = (ClampMin = "0.5", ClampMax = "60.0", UIMin = "1.0", UIMax = "20.0"))
	float DirectorTickHz = 5.f;

	/**
	 * Maximum spawns the director performs in a single pacing tick. Spreads a large simultaneous wave
	 * across a few ticks so one frame never instantiates an unbounded number of actors.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Spawn|Pacing", meta = (ClampMin = "1", ClampMax = "256", UIMin = "1", UIMax = "32"))
	int32 MaxSpawnsPerTick = 8;

	// ---- Squad sizing & formation ----

	/**
	 * Default maximum members a squad accepts before the subsystem refuses to add more (a designer can
	 * still author tighter caps per squad through the API). Bounds formation-slot allocation.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Squad", meta = (ClampMin = "1", ClampMax = "64", UIMin = "2", UIMax = "16"))
	int32 DefaultMaxSquadSize = 6;

	/**
	 * Spacing (world units) between adjacent slots in the fallback grid formation the squad subsystem
	 * generates when no explicit formation data is supplied. Pure layout tunable.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Squad", meta = (ClampMin = "10.0", ClampMax = "2000.0", UIMin = "50.0", UIMax = "500.0"))
	float FallbackFormationSpacing = 150.f;

	/**
	 * Number of columns in the fallback grid formation. Members fill row-major; the row count is derived
	 * from the member count and this column count.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Squad", meta = (ClampMin = "1", ClampMax = "16", UIMin = "1", UIMax = "8"))
	int32 FallbackFormationColumns = 3;

	// ---- Fallback spawn region (when no ILvl_SpawnRegionProvider is registered) ----

	/**
	 * Radius (world units) of the disc the director samples around a fallback region point when no
	 * level region provider answers a spawn-region tag. Keeps fallback spawns from stacking on one spot.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Spawn|Fallback", meta = (ClampMin = "0.0", ClampMax = "10000.0", UIMin = "0.0", UIMax = "2000.0"))
	float FallbackRegionRadius = 300.f;

	// ---- Tactical depth: spatial query ----

	/**
	 * Default grid spacing (world units) the query subsystem uses when a UAI_EnvQuery leaves GridSpacing
	 * at/below zero. Safety net only; queries normally carry their own spacing.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Query", meta = (ClampMin = "1.0", ClampMax = "2000.0", UIMin = "25.0", UIMax = "500.0"))
	float DefaultQueryGridSpacing = 100.f;

	/**
	 * Hard ceiling on candidate points any single query may generate/score, regardless of a query asset's
	 * MaxItems, to protect the frame from a mis-authored huge grid.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Query", meta = (ClampMin = "1", ClampMax = "16384", UIMin = "64", UIMax = "4096"))
	int32 MaxQueryItemsHardCap = 1024;

	// ---- Tactical depth: cover ----

	/**
	 * Default search radius (world units) the cover component uses when its own SearchRadius resolves to
	 * zero. Bounds how far an agent will look for cover.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Cover", meta = (ClampMin = "0.0", ClampMax = "10000.0", UIMin = "200.0", UIMax = "3000.0"))
	float DefaultCoverSearchRadius = 1500.f;

	/**
	 * Weight applied to a cover point's protection-direction quality versus its distance to the seeker when
	 * scoring cover (0 = distance only, 1 = protection only). Pure scoring tunable.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Cover", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CoverProtectionWeight = 0.6f;

	// ---- Tactical depth: positioning ----

	/**
	 * Default preferred engagement range (world units) positioning strategies fall back to when their own
	 * PreferredRange resolves to zero. Genre-neutral.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Positioning", meta = (ClampMin = "1.0", ClampMax = "10000.0", UIMin = "100.0", UIMax = "2000.0"))
	float DefaultPreferredRange = 400.f;

	// ---- Tactical depth: patrol ----

	/**
	 * Default acceptance radius (world units) a patrol component uses to consider a waypoint reached when
	 * its own AcceptanceRadius resolves to zero.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Patrol", meta = (ClampMin = "1.0", ClampMax = "2000.0", UIMin = "25.0", UIMax = "300.0"))
	float DefaultPatrolAcceptanceRadius = 100.f;

	/** Convenience accessor (never null in a running game; the CDO carries the config). */
	static const UAI_DeveloperSettings* Get();
};
