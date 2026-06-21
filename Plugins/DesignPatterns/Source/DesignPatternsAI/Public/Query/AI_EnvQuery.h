// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Curves/CurveFloat.h"
#include "Engine/EngineTypes.h"
#include "Identity/Seam_EntityId.h"
#include "AI_EnvQuery.generated.h"

// The engine EQS asset type belongs to AIModule (a PRIVATE dependency). Forward-declare it here so the
// optional bridge soft-ref can live in this PUBLIC header without dragging AIModule into the link line;
// "EnvironmentQuery/EnvQuery.h" is included only in the .cpp.
class UEnvQuery;

/**
 * How a query lays out the candidate points it scores. Genre-neutral spatial generators only; nothing
 * about a specific game's geometry is baked in (radii / spacing are tunables on UAI_EnvQuery).
 */
UENUM(BlueprintType)
enum class EAI_QueryGenerator : uint8
{
	/** A square grid of points around the query center, spaced by GridSpacing out to GenerationRadius. */
	Grid,

	/** A ring of points on a circle of radius GenerationRadius around the center (MaxItems samples). */
	Ring,

	/** Points sampled on actors already registered with the cover subsystem (cover-point candidates). */
	CoverPoints,

	/** A single point: the query center itself (useful to score one candidate, e.g. "is here good?"). */
	SinglePoint
};

/**
 * Read-only context handed to every test when a query runs. Plain value USTRUCT — never networked, never
 * GC-allocated per query; passed by const ref. Holds only a weak querier ref plus value-type spatial data.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_QueryContext
{
	GENERATED_BODY()

	/** The actor running the query (weak: a destroyed querier never keeps the context alive). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|AI|Query")
	TWeakObjectPtr<AActor> Querier;

	/** World location the query is generated around (defaults to the querier's location). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|AI|Query")
	FVector QuerierLocation = FVector::ZeroVector;

	/** The relevant target/threat world location (e.g. enemy to flank or take cover from). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|AI|Query")
	FVector TargetLocation = FVector::ZeroVector;

	/** Stable id of the target entity, if any (lets tests resolve the target actor without a hard ref). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|AI|Query")
	FSeam_EntityId TargetId;

	/** What the query is FOR (e.g. AI.Query.Purpose.Cover) so tests can branch generically. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|AI|Query")
	FGameplayTag Purpose;

	FAI_QueryContext() = default;
};

/**
 * One scored candidate point produced by a query run. Plain value USTRUCT; never networked.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_ScoredPoint
{
	GENERATED_BODY()

	/** Candidate world location. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Query")
	FVector Location = FVector::ZeroVector;

	/** The actor this point belongs to, if it was generated from one (e.g. a cover point). Weak. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Query")
	TWeakObjectPtr<AActor> ItemActor;

	/** Normalized aggregate score in [0,1] after all weighted tests. Higher is better. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Query")
	float Score = 0.f;

	/** False if a hard filter (IsItemValid) rejected this point; such points are dropped before ranking. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Query")
	bool bValid = false;

	FAI_ScoredPoint() = default;
	FAI_ScoredPoint(const FVector& InLocation, AActor* InActor)
		: Location(InLocation), ItemActor(InActor), bValid(true) {}
};

/**
 * One weighted EQS-style scoring test authored INLINE on a UAI_EnvQuery, mirroring UDP_Strategy's
 * EditInlineNew + BlueprintNativeEvent authoring so tests can be subclassed in C++ or Blueprint.
 *
 * PURITY: a test is side-effect free (the subsystem polls it freely over every candidate). It is
 * world-context-free except via Context.Querier — it must resolve any service (e.g. cover) through the
 * GameInstance service locator derived from Context.Querier->GetWorld(), never from a stored world ptr.
 *
 * SCORING CONTRACT: ScoreItem returns a RAW score; the subsystem normalizes per-test scores across all
 * candidates to [0,1], optionally remaps through ResponseCurve, applies bInvert, then weights by Weight
 * and sums. This keeps each test's raw output free to be in any range.
 */
UCLASS(Abstract, EditInlineNew, DefaultToInstanced, Blueprintable, CollapseCategories)
class DESIGNPATTERNSAI_API UAI_QueryTest : public UObject
{
	GENERATED_BODY()

public:
	/** Relative weight of this test in the weighted sum. Tests with Weight <= 0 are skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query", meta = (ClampMin = "0.0"))
	float Weight = 1.f;

	/** When true the normalized per-test score is flipped (1 - score) before weighting (e.g. "far is good"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query")
	bool bInvert = false;

	/**
	 * Optional response remap applied to the per-candidate normalized [0,1] score before weighting. When
	 * left at its default (no keys) the normalized score is used directly (linear). Lets designers shape
	 * "ideal band" responses without code.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query")
	FRuntimeFloatCurve ResponseCurve;

	/**
	 * Produce this test's RAW score for one candidate. Higher is better (the subsystem normalizes across
	 * candidates afterwards). Must be side-effect free.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|AI|Query")
	float ScoreItem(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const;
	virtual float ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const;

	/**
	 * HARD filter: return false to reject the candidate outright (it is dropped before ranking, regardless
	 * of score). Default: accept everything.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|AI|Query")
	bool IsItemValid(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const;
	virtual bool IsItemValid_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const;

	/**
	 * Apply this test's normalization+curve+invert to a raw value given the run's raw min/max. Shared by
	 * the subsystem so every test normalizes consistently. @return the weighted contribution in [0, Weight].
	 */
	float Finalize(float RawScore, float RawMin, float RawMax) const;

protected:
	/** Resolve the GameInstance service locator from the context's querier world (null-safe). */
	class UDP_ServiceLocatorSubsystem* ResolveLocator(const FAI_QueryContext& Context) const;
};

/**
 * Pure-DATA spatial query: a generator + an inline array of weighted UAI_QueryTest. Genre-agnostic; no
 * gameplay magic numbers in code (radii / spacing / item caps are all designer fields here).
 *
 * Run by UAI_QuerySubsystem with the built-in scorer by default. An OPTIONAL engine-EQS bridge is exposed
 * as a soft ref (EngineEqsAsset, gated by bPreferEngineEQS) — the engine UEnvQuery type is forward-declared
 * so AIModule stays a private dependency; the bridge is resolved purely at runtime in the subsystem .cpp.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAI_API UAI_EnvQuery : public UDataAsset
{
	GENERATED_BODY()

public:
	/** The weighted tests, authored inline. Evaluated in order; their normalized scores are summed. */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "DesignPatterns|AI|Query")
	TArray<TObjectPtr<UAI_QueryTest>> Tests;

	/** How candidate points are generated before scoring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query")
	EAI_QueryGenerator Generator = EAI_QueryGenerator::Grid;

	/** Half-extent of the generation area (world units): grid half-size or ring radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query", meta = (ClampMin = "0.0"))
	float GenerationRadius = 500.f;

	/** Spacing between adjacent grid points (world units). Only used by the Grid generator. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query", meta = (ClampMin = "1.0"))
	float GridSpacing = 100.f;

	/** Hard cap on candidate points generated/scored (protects the scorer from a huge grid). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query", meta = (ClampMin = "1", ClampMax = "4096"))
	int32 MaxItems = 256;

	/**
	 * When true AND EngineEqsAsset is set AND AIModule's EQS manager is available at runtime, the subsystem
	 * delegates to the engine EQS instead of the built-in scorer. Otherwise the built-in scorer runs.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|EngineBridge")
	bool bPreferEngineEQS = false;

	/** Optional engine EQS asset for the bridge. Soft ref so AIModule stays out of the link line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|EngineBridge")
	TSoftObjectPtr<UEnvQuery> EngineEqsAsset;
};
