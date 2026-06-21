// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Query/AI_EnvQuery.h"
#include "Engine/EngineTypes.h"
#include "AI_QueryTests.generated.h"

/**
 * Score by distance to a reference point. Peaks at IdealDistance and falls off toward 0 at MaxDistance.
 * Reference is the querier by default, or the target when ReferenceIsTarget is true. No magic numbers —
 * distances are designer fields.
 */
UCLASS(meta = (DisplayName = "Distance"))
class DESIGNPATTERNSAI_API UAI_QueryTest_Distance : public UAI_QueryTest
{
	GENERATED_BODY()

public:
	/** When true the reference point is Context.TargetLocation; otherwise Context.QuerierLocation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|Distance")
	bool bReferenceIsTarget = false;

	/** Distance (world units) at which the score peaks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|Distance", meta = (ClampMin = "0.0"))
	float IdealDistance = 300.f;

	/** Distance (world units) beyond/under which the score reaches 0 (triangular falloff around Ideal). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|Distance", meta = (ClampMin = "1.0"))
	float MaxDistance = 1500.f;

	virtual float ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const override;
};

/**
 * HARD-filter + score by line of sight from the candidate point to a reference point (querier or target).
 * Points without LoS are rejected (IsItemValid=false) when bRequireLoS; otherwise no-LoS scores 0.
 * Trace runs on the world's collision against TraceChannel in the .cpp.
 */
UCLASS(meta = (DisplayName = "Line Of Sight"))
class DESIGNPATTERNSAI_API UAI_QueryTest_LineOfSight : public UAI_QueryTest
{
	GENERATED_BODY()

public:
	/** When true LoS is tested to Context.TargetLocation; otherwise to Context.QuerierLocation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|LoS")
	bool bToTarget = true;

	/** When true, a candidate with NO line of sight is hard-rejected instead of merely scoring 0. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|LoS")
	bool bRequireLoS = false;

	/** Vertical eye offset added to both endpoints so the trace approximates eye height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|LoS", meta = (ClampMin = "0.0"))
	float EyeHeight = 64.f;

	/** Collision channel the visibility trace runs against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|LoS")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	virtual bool IsItemValid_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const override;
	virtual float ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const override;

protected:
	/** Shared LoS evaluation: true if the candidate can see the reference point. */
	bool HasLineOfSight(const FAI_QueryContext& Context, const FVector& ItemLocation) const;
};

/**
 * Alias-style test that scores HIGHER the better a candidate can see the TARGET specifically — useful for
 * firing positions. Distinct from _LineOfSight (which can reference the querier) by always targeting the
 * target and scoring by a clear-shot factor rather than a binary.
 */
UCLASS(meta = (DisplayName = "LoS To Target"))
class DESIGNPATTERNSAI_API UAI_QueryTest_LosToTarget : public UAI_QueryTest
{
	GENERATED_BODY()

public:
	/** Vertical eye offset added to both endpoints. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|LoS", meta = (ClampMin = "0.0"))
	float EyeHeight = 64.f;

	/** Collision channel the visibility trace runs against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|LoS")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	/** Candidates with no LoS to the target are hard-rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|LoS")
	bool bRequireLoS = true;

	virtual bool IsItemValid_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const override;
	virtual float ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const override;
};

/**
 * Score by local agent DENSITY: counts ISeam_TeamAffinity-friendly agents near the candidate, so the
 * scorer can prefer spreading out (invert) or clustering. Uses an overlap query in the .cpp.
 */
UCLASS(meta = (DisplayName = "Density"))
class DESIGNPATTERNSAI_API UAI_QueryTest_Density : public UAI_QueryTest
{
	GENERATED_BODY()

public:
	/** Radius (world units) within which friendly agents are counted around each candidate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|Density", meta = (ClampMin = "1.0"))
	float CountRadius = 400.f;

	/** When true, only ISeam_TeamAffinity-friendly pawns are counted; otherwise all pawns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|Density")
	bool bFriendlyOnly = true;

	/** Object type to overlap for the density count. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|Density")
	TEnumAsByte<ECollisionChannel> OverlapChannel = ECC_Pawn;

	virtual float ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const override;
};

/**
 * Score by how good a candidate is as COVER from the target/threat, resolving the cover provider seam
 * (ISeam_CoverProvider) via DP.Service.AI.Cover off Context.Querier->GetWorld(). When no provider is
 * registered the test contributes a neutral 0 (no cover info) rather than crashing.
 */
UCLASS(meta = (DisplayName = "Cover Score"))
class DESIGNPATTERNSAI_API UAI_QueryTest_CoverScore : public UAI_QueryTest
{
	GENERATED_BODY()

public:
	/** When true the threat reference is Context.TargetLocation; otherwise Context.QuerierLocation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Query|Cover")
	bool bThreatIsTarget = true;

	virtual float ScoreItem_Implementation(const FAI_QueryContext& Context, const FVector& ItemLocation, AActor* ItemActor) const override;
};
