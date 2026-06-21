// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Identity/Seam_EntityId.h"
#include "Seam_CoverProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class USeam_CoverProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * READ seam onto a world cover system. Implemented by UAI_CoverSubsystem and resolved by consumers as a
 * TScriptInterface<ISeam_CoverProvider> from the service locator (DP.Service.AI.Cover) — so combat, EQS
 * tests, positioning strategies and per-agent cover behaviour find cover WITHOUT depending on the AI
 * module's concrete cover types.
 *
 * VALUE-TYPE BOUNDARY (critical): every method exchanges only net/save-safe value types — FVector,
 * FTransform and FSeam_EntityId. The seam NEVER returns the concrete AAI_CoverPoint*, so a caller across
 * the seam never has to include (and never links against) the AI module's cover actor. The owning agent
 * resolves the concrete point from the returned id internally when it needs to claim/release it.
 *
 * AUTHORITY: pure reads (FindBestCover / ScoreCoverAt) are safe on any machine — they only inspect the
 * world index. CLAIMING a cover point is an authoritative mutation that lives on AAI_CoverPoint / the
 * cover component, NOT on this read seam (mirroring the IAI_Squad "read seam vs. authority mutator" split).
 */
class DESIGNPATTERNSSEAMS_API ISeam_CoverProvider
{
	GENERATED_BODY()

public:
	/**
	 * Find the best UNCLAIMED cover point near Origin that protects against fire coming from
	 * ThreatLocation, scored by distance to Origin and how well it shields the threat direction.
	 *
	 * @param Origin           Where the seeker is / wants cover near (world space).
	 * @param ThreatLocation   Where the danger is coming from (world space); shapes the protection score.
	 * @param Radius           Maximum search radius around Origin (world units). <= 0 means "no radius limit".
	 * @param OutCoverTransform Receives the world transform the agent should occupy (the cover point's
	 *                          stand/peek transform). Untouched when the call returns false.
	 * @param OutCoverId       Receives the stable id of the chosen cover point (used later to claim it).
	 *                          Untouched when the call returns false.
	 * @return true if a suitable cover point was found.
	 */
	virtual bool FindBestCover(const FVector& Origin, const FVector& ThreatLocation, float Radius,
		FTransform& OutCoverTransform, FSeam_EntityId& OutCoverId) const = 0;

	/**
	 * Score how good a specific world location would be as cover from ThreatLocation. Higher is better;
	 * <= 0 means "useless / exposed". Used by EQS-style scoring tests to weight candidate points without
	 * needing the concrete cover index. Pure read, side-effect free.
	 *
	 * @param Location        Candidate world location to evaluate.
	 * @param ThreatLocation  Where the danger is coming from (world space).
	 * @return a non-negative cover quality score (0 = no cover).
	 */
	virtual float ScoreCoverAt(const FVector& Location, const FVector& ThreatLocation) const = 0;
};
