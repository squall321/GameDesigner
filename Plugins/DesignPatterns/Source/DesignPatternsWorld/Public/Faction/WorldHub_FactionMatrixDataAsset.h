// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Faction/WorldHub_FactionTypes.h"
#include "WorldHub_FactionMatrixDataAsset.generated.h"

/**
 * The authored configuration of a faction-standing matrix: initial pairs, numeric bounds, the
 * symmetry policy, discrete tier thresholds and the replicate/save policy for the projected hub keys.
 *
 * All gameplay numbers are data here (no magic constants in C++): clamp range, hostility threshold,
 * tiers and the projected-key policy are all editable. Subclass of UDP_DataAsset so it is
 * tag-addressable through the data registry.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSWORLD_API UWorldHub_FactionMatrixDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/** Initial standings applied (and clamped) when the matrix component begins play on the authority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	TArray<FWorldHub_FactionStandingSeed> InitialStandings;

	/** Inclusive minimum standing value (clamp lower bound). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	float MinStanding = -100.0f;

	/** Inclusive maximum standing value (clamp upper bound). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	float MaxStanding = 100.0f;

	/** Default standing for an un-seeded (A,B) pair (also clamped to [Min,Max]). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	float DefaultStanding = 0.0f;

	/** Standings strictly below this threshold are considered hostile (drives AreHostile). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	float HostileBelow = 0.0f;

	/** Whether writing A->B mirrors B->A. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	EWorldHub_FactionSymmetry Symmetry = EWorldHub_FactionSymmetry::Symmetric;

	/**
	 * Discrete tiers in ASCENDING MinStanding order (the data validator can warn if unsorted). A
	 * standing maps to the highest tier whose MinStanding it meets.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	TArray<FWorldHub_FactionTier> Tiers;

	/** Whether projected standing keys are mirrored to clients via the hub's rep carrier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	bool bReplicateStandings = true;

	/** Whether projected standing keys are captured into the save game. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|WorldHub|Faction")
	bool bSaveStandings = true;

	/** Clamp a raw standing into the authored [MinStanding, MaxStanding] range. */
	float ClampStanding(float Raw) const { return FMath::Clamp(Raw, MinStanding, MaxStanding); }

	/** Classify a standing into a tier tag using the authored thresholds (invalid tag if none match). */
	FGameplayTag ClassifyTier(float Standing) const
	{
		FGameplayTag Best;
		float BestMin = -TNumericLimits<float>::Max();
		for (const FWorldHub_FactionTier& Tier : Tiers)
		{
			if (Standing >= Tier.MinStanding && Tier.MinStanding >= BestMin && Tier.TierTag.IsValid())
			{
				Best = Tier.TierTag;
				BestMin = Tier.MinStanding;
			}
		}
		return Best;
	}
};
