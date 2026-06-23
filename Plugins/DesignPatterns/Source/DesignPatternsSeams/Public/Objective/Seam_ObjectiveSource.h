// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_ObjectiveSource.generated.h"

/**
 * One tracked/pinned objective as a HUD objective tracker consumes it, in seam-neutral terms.
 *
 * Primitives + tags + an FText title ONLY (no quest/mission module types) so the Seams module stays a leaf
 * and an objective HUD can read live quest state WITHOUT depending on the concrete quest / hub system. The
 * game's quest system fills these; the HUD projects each into a tracker row and, when bHasWorldLocation,
 * may also drive a world waypoint marker.
 *
 * FText is permitted here (Core, not Slate) — the title is already-localized display text; the leaf
 * invariant only forbids Slate / UMG, not FText.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_ObjectiveSnapshot
{
	GENERATED_BODY()

	/** Stable identity tag for this objective (pin/unpin key + marker correlation). Empty entries are skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Objective")
	FGameplayTag ObjectiveId;

	/** Already-localized display title for the tracker row. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Objective")
	FText Title;

	/** Current progress value (e.g. 3 of 5 collected). 0 when the objective is not progress-based. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Objective")
	float ProgressCurrent = 0.f;

	/** Target progress value (e.g. 5). <= 0 means "no numeric progress" (a binary objective). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Objective")
	float ProgressTarget = 0.f;

	/** Coarse state tag (e.g. DP.Objective.State.Active / Complete / Failed) for styling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Objective")
	FGameplayTag StateTag;

	/** True when WorldLocation is meaningful (the HUD may then place a world waypoint for this objective). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Objective")
	bool bHasWorldLocation = false;

	/** World-space location of the objective (only valid when bHasWorldLocation). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Objective")
	FVector WorldLocation = FVector::ZeroVector;

	FSeam_ObjectiveSnapshot() = default;

	/** True when this snapshot carries a valid objective id (used to cull empty rows). */
	bool IsValidObjective() const { return ObjectiveId.IsValid(); }

	/**
	 * Normalized progress in [0,1]; returns 0 for a binary (non-numeric) objective so a progress bar
	 * reads "empty" rather than dividing by zero. A completed binary objective is signalled via StateTag.
	 */
	float GetProgressFraction() const
	{
		if (ProgressTarget <= 0.f)
		{
			return 0.f;
		}
		return FMath::Clamp(ProgressCurrent / ProgressTarget, 0.f, 1.f);
	}

	bool operator==(const FSeam_ObjectiveSnapshot& Other) const
	{
		return ObjectiveId == Other.ObjectiveId
			&& StateTag == Other.StateTag
			&& bHasWorldLocation == Other.bHasWorldLocation
			&& Title.IdenticalTo(Other.Title)
			&& FMath::IsNearlyEqual(ProgressCurrent, Other.ProgressCurrent, KINDA_SMALL_NUMBER)
			&& FMath::IsNearlyEqual(ProgressTarget, Other.ProgressTarget, KINDA_SMALL_NUMBER)
			&& (!bHasWorldLocation || WorldLocation.Equals(Other.WorldLocation, 1.f));
	}
	bool operator!=(const FSeam_ObjectiveSnapshot& Other) const { return !(*this == Other); }
};

UINTERFACE(MinimalAPI, BlueprintType, meta = (DisplayName = "Seam Objective Source"))
class USeam_ObjectiveSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read seam exposing the game's currently-tracked / pinned objectives in seam-neutral terms.
 *
 * Implemented by the game's quest / mission / hub system and resolved by the HUD as a TScriptInterface via
 * a project service-locator key. The HUD objective tracker polls it (and/or listens to a quest bus channel)
 * to project tracker rows and optional world waypoints, WITHOUT depending on the concrete quest module.
 *
 * All reads are cheap, side-effect-free projections of already-known state (BlueprintNativeEvent so a
 * project can author a source in Blueprint, matching the IHUD_Trackable / ISeam_StatusProvider house
 * style). Authority-agnostic pure reads.
 */
class ISeam_ObjectiveSource
{
	GENERATED_BODY()

public:
	/**
	 * Fill Out with every currently-tracked objective, in the source's display order. Implementations should
	 * Reset() Out first; a source with nothing tracked returns an empty array (not an error).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Objective")
	void GetTrackedObjectives(TArray<FSeam_ObjectiveSnapshot>& Out) const;

	/**
	 * Fetch a single objective by id. Returns true and fills Out when Id is tracked; returns false (leaving
	 * Out default) otherwise. Lets the HUD refresh one pinned objective without snapshotting the whole set.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Objective")
	bool GetObjectiveById(FGameplayTag Id, FSeam_ObjectiveSnapshot& Out) const;
};
