// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Engine/EngineTypes.h"
#include "Lvl_PlacementRuleSet.generated.h"

class AActor;

/** How a placement pass derives its candidate positions before validation. */
UENUM(BlueprintType)
enum class ELvl_PlacementSource : uint8
{
	/** Scatter candidates uniformly (jittered grid) inside the placer's local box extent. */
	BoxVolume,

	/** Scatter candidates uniformly inside a disc of LocalRadius around the placer. */
	RadialArea,

	/**
	 * Walk a spline component on the placer's owner, sampling candidates along its length at
	 * SplineSpacing intervals (with lateral jitter). Falls back to BoxVolume if the owner has no
	 * usable spline.
	 */
	SplinePath
};

/**
 * One weighted actor-class choice for a scatter pass. The class is identified BOTH by a stable
 * factory identity tag (preferred: routes through the core UDP_SpawnFactorySubsystem and pool) and
 * an optional soft class (direct fallback when no factory is registered for the tag). Storing the
 * tag in the manifest keeps saves path-free and rename-safe.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSLEVELDIRECTOR_API FLvl_PlacementClassChoice
{
	GENERATED_BODY()

	/**
	 * Stable identity used for both spawning (factory lookup) and the saved manifest. Required —
	 * a choice with no identity tag is skipped (the editor validator flags it).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Placement")
	FGameplayTag ActorClassTag;

	/**
	 * Direct class used only when no factory is registered for ActorClassTag. Soft so the rule set
	 * can be scanned/cooked without loading gameplay actor code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Placement", meta = (AllowAbstract = "false"))
	TSoftClassPtr<AActor> ActorClass;

	/** Relative selection weight among the rule set's choices. Clamped to >= 0; all-zero -> uniform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Placement", meta = (ClampMin = "0.0"))
	float Weight = 1.0f;

	/**
	 * Per-choice vertical offset (cm) applied after the surface projection trace, e.g. to sink a
	 * rock slightly or float a marker. Added along the projected surface normal's up component.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Placement")
	float VerticalOffset = 0.0f;

	FLvl_PlacementClassChoice() = default;

	bool IsUsable() const { return ActorClassTag.IsValid(); }
};

/**
 * A designer-authored rule set describing HOW to scatter/place actors for one procedural pass.
 *
 * This is DATA ONLY — no behaviour. The ULvl_ProceduralPlacerComponent reads a rule set and
 * performs a deterministic pass from it. EVERY tunable lives here as an EditAnywhere property
 * (no magic numbers in the placer), so designers tune density/spacing/masks without touching code.
 *
 * Determinism: RandomSeed (plus the owner's stable name) seeds an FRandomStream so the same rule
 * set on the same actor produces the same layout every run and across save/load. A seed of 0 means
 * "derive from the owner name hash" (still deterministic, but unique per placer).
 *
 * Tile masks: candidates are validated against the read-only grid seam (ISeam_TileProviderRead).
 * AllowedTileTypes / BlockedTileTypes are matched against the cell snapshot's TileTypeTag. Because
 * the tile seam exposes no height, the placer projects each candidate onto the world surface with a
 * downward trace (channel + reach configured here) — the tile seam answers "may I place here?",
 * the trace answers "at what height/orientation?".
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_PlacementRuleSet : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	ULvl_PlacementRuleSet();

	// ---- Identity / region ----------------------------------------------------------------------

	/**
	 * Logical region/owner this rule set is meant for. Copied into the manifest's RegionTag so
	 * save/restore routes results back to the correct placer. Optional — a placer may override it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Identity")
	FGameplayTag DefaultRegionTag;

	// ---- Determinism ----------------------------------------------------------------------------

	/**
	 * Seed for the deterministic FRandomStream. 0 -> derive from the owning actor's stable name so
	 * each placer still gets a reproducible-but-unique layout. Non-zero -> exact shared seed.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Determinism")
	int32 RandomSeed = 0;

	// ---- Source / area --------------------------------------------------------------------------

	/** How candidate positions are derived before validation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Source")
	ELvl_PlacementSource Source = ELvl_PlacementSource::BoxVolume;

	/** Local half-extent of the scatter box (cm) when Source == BoxVolume. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Source",
		meta = (EditCondition = "Source == ELvl_PlacementSource::BoxVolume", ClampMin = "0.0"))
	FVector BoxExtent = FVector(2000.0, 2000.0, 0.0);

	/** Local radius of the scatter disc (cm) when Source == RadialArea. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Source",
		meta = (EditCondition = "Source == ELvl_PlacementSource::RadialArea", ClampMin = "0.0"))
	float LocalRadius = 2000.0f;

	/** Spacing (cm) between samples taken along the spline when Source == SplinePath. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Source",
		meta = (EditCondition = "Source == ELvl_PlacementSource::SplinePath", ClampMin = "1.0"))
	float SplineSpacing = 400.0f;

	/** Maximum lateral jitter (cm) applied either side of a spline sample. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Source",
		meta = (EditCondition = "Source == ELvl_PlacementSource::SplinePath", ClampMin = "0.0"))
	float SplineLateralJitter = 150.0f;

	// ---- Density / count ------------------------------------------------------------------------

	/**
	 * Target number of CANDIDATES generated per square metre of source area (before validation
	 * rejects any). Drives candidate count for BoxVolume / RadialArea sources. Validation/spacing
	 * thins this down, so the placed count is typically lower.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Density",
		meta = (ClampMin = "0.0"))
	float DensityPerSquareMetre = 0.02f;

	/** Hard upper bound on candidates considered in a pass (safety cap against huge areas). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Density",
		meta = (ClampMin = "1"))
	int32 MaxCandidates = 2048;

	/** Hard upper bound on actors actually placed in a pass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Density",
		meta = (ClampMin = "0"))
	int32 MaxPlacements = 256;

	// ---- Spacing --------------------------------------------------------------------------------

	/**
	 * Minimum distance (cm) required between any two PLACED actors. A candidate closer than this to
	 * an already-accepted placement is rejected (Poisson-disk style thinning). 0 disables spacing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Spacing",
		meta = (ClampMin = "0.0"))
	float MinSpacing = 300.0f;

	// ---- Tile masks (via ISeam_TileProviderRead) ------------------------------------------------

	/**
	 * If non-empty, a candidate's grid cell must report a TileTypeTag that matches (is a child of)
	 * one of these to be accepted. Empty -> no allow-list (any tile passes the allow check).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|TileMask")
	FGameplayTagContainer AllowedTileTypes;

	/** A candidate whose cell TileTypeTag matches any of these is rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|TileMask")
	FGameplayTagContainer BlockedTileTypes;

	/**
	 * If true, a candidate whose cell the tile provider reports as Unknown (e.g. a client without
	 * replication, or out-of-bounds) is rejected. If false, Unknown cells pass the tile check (the
	 * surface trace still has the final say). Placement runs authority-only, where cells are known,
	 * so this mainly governs out-of-bounds candidates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|TileMask")
	bool bRejectUnknownCells = true;

	// ---- Surface projection (downward trace) ----------------------------------------------------

	/** If true, each candidate is projected onto the world surface by a downward line trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Surface")
	bool bProjectToSurface = true;

	/** Trace channel used for the surface projection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Surface",
		meta = (EditCondition = "bProjectToSurface"))
	TEnumAsByte<ECollisionChannel> SurfaceTraceChannel = ECC_WorldStatic;

	/** Height (cm) above the candidate the downward trace starts from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Surface",
		meta = (EditCondition = "bProjectToSurface", ClampMin = "0.0"))
	float TraceStartHeight = 5000.0f;

	/** Total downward distance (cm) the projection trace travels from its start height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Surface",
		meta = (EditCondition = "bProjectToSurface", ClampMin = "1.0"))
	float TraceDistance = 20000.0f;

	/**
	 * If true and the projection trace misses (no surface), the candidate is rejected. If false, a
	 * missed trace keeps the candidate at its un-projected height (useful for flying/space content).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Surface",
		meta = (EditCondition = "bProjectToSurface"))
	bool bRejectOnTraceMiss = true;

	/**
	 * Maximum surface slope (degrees from horizontal) a candidate may sit on. A surface steeper than
	 * this is rejected. 90 effectively disables the slope check.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Surface",
		meta = (EditCondition = "bProjectToSurface", ClampMin = "0.0", ClampMax = "90.0"))
	float MaxSlopeDegrees = 35.0f;

	/** If true, the placed actor is rotated so its up axis aligns to the projected surface normal. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Surface",
		meta = (EditCondition = "bProjectToSurface"))
	bool bAlignToSurfaceNormal = false;

	// ---- Transform variation --------------------------------------------------------------------

	/** If true, each placement gets a random yaw in [0, 360). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Variation")
	bool bRandomYaw = true;

	/** Minimum uniform scale applied to a placement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Variation",
		meta = (ClampMin = "0.01"))
	float MinScale = 1.0f;

	/** Maximum uniform scale applied to a placement (clamped to >= MinScale at use). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Variation",
		meta = (ClampMin = "0.01"))
	float MaxScale = 1.0f;

	// ---- Class table ----------------------------------------------------------------------------

	/** Weighted set of actor classes a placement may instantiate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Classes")
	TArray<FLvl_PlacementClassChoice> ClassChoices;

	/**
	 * GateKey the whole pass is gated on via ISeam_ActivationGate (default open when the gate seam is
	 * unresolved). If unset, the pass is ungated. Lets designers suppress dressing for a closed area.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Lvl|Placement|Gate")
	FGameplayTag GateKey;

	// ---- Derived helpers ------------------------------------------------------------------------

	/**
	 * Pick a weighted class choice using the supplied stream (deterministic). Returns nullptr if the
	 * table is empty or every usable choice has zero weight (then the caller should pick uniformly).
	 */
	const FLvl_PlacementClassChoice* PickClassChoice(FRandomStream& Stream) const;

	/** Sum of usable choice weights (>= 0). Used by PickClassChoice. */
	float GetTotalChoiceWeight() const;

	/** True if the rule set has at least one usable class choice. */
	bool HasUsableClass() const;

	/** Clamp helper: the effective [min,max] uniform scale with max >= min guaranteed. */
	void GetClampedScaleRange(float& OutMin, float& OutMax) const;

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
