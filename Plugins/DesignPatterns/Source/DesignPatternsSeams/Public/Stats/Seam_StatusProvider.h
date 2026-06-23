// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_StatusProvider.generated.h"

/**
 * One active timed status / buff / debuff entry, in seam-neutral terms.
 *
 * Primitives + tags ONLY (no FSeam_StatMod, no cross-module gameplay types) so the Seams module stays a
 * leaf and a HUD status bar can read live buff state WITHOUT depending on the concrete stats / ability
 * system. The status system fills these per active effect; the HUD projects each into a view row,
 * computing the duration-ring fraction from RemainingSeconds / TotalSeconds.
 *
 * Co-located with Seam_StatMod.h under Stats/ (rather than a near-duplicate "Status" folder) because it
 * describes the same domain — active stat effects — read in a presentation-oriented form. It deliberately
 * does NOT reuse FSeam_StatMod: a status bar needs timing/stack metadata (remaining/total/stacks/category)
 * that a raw stat modifier does not carry, and must not be coupled to the modifier's Op/Magnitude shape.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_StatusEntry
{
	GENERATED_BODY()

	/** Identity / icon-selecting tag for this status (e.g. DP.Status.Buff.Haste). Empty entries are skipped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Status")
	FGameplayTag StatusTag;

	/** Current stack count (1 for a non-stacking status). A status bar may render this as a badge. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Status")
	int32 Stacks = 1;

	/** Seconds remaining before this status expires; <= 0 for an indefinite / permanent status. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Status")
	float RemainingSeconds = 0.f;

	/** Full duration the status was applied for; <= 0 for indefinite. Used with RemainingSeconds for the ring. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Status")
	float TotalSeconds = 0.f;

	/** Coarse classification (e.g. DP.Status.Category.Buff / Debuff / Curse) for grouping / coloring. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Status")
	FGameplayTag CategoryTag;

	/** Optional scalar magnitude (e.g. +20% as 0.2) for a text readout. Presentation-only; 0 when unused. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Status")
	float Magnitude = 0.f;

	FSeam_StatusEntry() = default;

	/** True when this entry carries a valid status tag (used to cull empty rows). */
	bool IsValidEntry() const { return StatusTag.IsValid(); }

	/**
	 * Normalized remaining fraction in [0,1] for a duration ring; returns 1 for an indefinite status
	 * (TotalSeconds <= 0) so the ring reads "full" rather than dividing by zero.
	 */
	float GetRemainingFraction() const
	{
		if (TotalSeconds <= 0.f)
		{
			return 1.f;
		}
		return FMath::Clamp(RemainingSeconds / TotalSeconds, 0.f, 1.f);
	}

	bool operator==(const FSeam_StatusEntry& Other) const
	{
		return StatusTag == Other.StatusTag
			&& Stacks == Other.Stacks
			&& CategoryTag == Other.CategoryTag
			&& FMath::IsNearlyEqual(RemainingSeconds, Other.RemainingSeconds, 0.05f)
			&& FMath::IsNearlyEqual(TotalSeconds, Other.TotalSeconds, 0.05f)
			&& FMath::IsNearlyEqual(Magnitude, Other.Magnitude, KINDA_SMALL_NUMBER);
	}
	bool operator!=(const FSeam_StatusEntry& Other) const { return !(*this == Other); }
};

UINTERFACE(MinimalAPI, BlueprintType, meta = (DisplayName = "Seam Status Provider"))
class USeam_StatusProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read seam exposing an actor's currently-active timed statuses (buffs/debuffs) in seam-neutral terms.
 *
 * Implemented by the game's status / ability component (usually on the pawn) and resolved by the HUD as a
 * TScriptInterface off the local pawn, OR published under a project service-locator key. The HUD status
 * bar polls it each refresh, projects FSeam_StatusEntry rows, and never touches the concrete stats module.
 *
 * All reads are cheap, side-effect-free projections of already-known state (BlueprintNativeEvent so a
 * project can author a provider in Blueprint, matching the IHUD_Trackable house style). Authority-agnostic:
 * status state is already replicated to each client, and this is a local read of it.
 */
class ISeam_StatusProvider
{
	GENERATED_BODY()

public:
	/**
	 * Fill Out with every currently-active status on this provider, in the provider's order. Implementations
	 * should Reset() Out first; a provider with no active statuses returns an empty array (not an error).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Status")
	void GetActiveStatuses(TArray<FSeam_StatusEntry>& Out) const;

	/**
	 * Convenience single-status query: the stack count of StatusTag (0 if not active). A status bar can use
	 * this for a focused readout without snapshotting the whole set.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Status")
	int32 GetStatusStacks(FGameplayTag StatusTag) const;
};
