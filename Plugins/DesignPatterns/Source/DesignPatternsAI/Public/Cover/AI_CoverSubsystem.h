// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Identity/Seam_EntityId.h"
#include "AI/Seam_CoverProvider.h"
#include "AI_CoverSubsystem.generated.h"

class AAI_CoverPoint;
class UDP_ServiceLocatorSubsystem;

/**
 * World index of AAI_CoverPoint actors, implementing the ISeam_CoverProvider read seam.
 *
 * Like UAI_SquadSubsystem indexes carriers, this rebuilds its index from the WORLD's live cover-point
 * actors (so clients pick up replicated points too) and answers best-cover queries by scoring candidates
 * — reusing UAI_QuerySubsystem's geometric reasoning where useful. It self-registers under
 * DP.Service.AI.Cover (WeakObserved) so combat / EQS tests / strategies resolve cover by tag.
 *
 * SEAM BOUNDARY: the ISeam_CoverProvider overrides return value types only (FTransform + FSeam_EntityId),
 * never the concrete AAI_CoverPoint*. A direct (AI-internal) overload that DOES return the concrete point
 * is provided for the cover component, which then performs the authoritative claim on it.
 */
UCLASS()
class DESIGNPATTERNSAI_API UAI_CoverSubsystem : public UDP_WorldSubsystem, public ISeam_CoverProvider
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** True on any non-pure-client net mode. Cover-point spawning gates on this. */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	// ---- ISeam_CoverProvider (value-type boundary; safe for cross-module callers) ----
	virtual bool FindBestCover(const FVector& Origin, const FVector& ThreatLocation, float Radius,
		FTransform& OutCoverTransform, FSeam_EntityId& OutCoverId) const override;
	virtual float ScoreCoverAt(const FVector& Location, const FVector& ThreatLocation) const override;

	// ---- AI-internal API (returns the concrete point so the owning agent can claim it) ----

	/**
	 * Find the best UNCLAIMED cover point near Origin shielding ThreatLocation, returning the concrete
	 * point. AI-internal (cover component) use only — cross-module callers use the seam overload above.
	 * @return the chosen point, or null if none qualifies.
	 */
	AAI_CoverPoint* FindBestCoverPoint(const FVector& Origin, const FVector& ThreatLocation, float Radius) const;

	/** Resolve a cover point by its stable id (rebuilds the index on a miss). */
	AAI_CoverPoint* FindCoverPointById(const FSeam_EntityId& CoverId) const;

	/**
	 * Spawn a cover point at Where with the given type tag and protected directions. AUTHORITY ONLY.
	 * Spawned points are transient runtime coordination state (DORM_Initial). @return the new point or null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Cover")
	AAI_CoverPoint* SpawnCoverPoint(const FTransform& Where, FGameplayTag CoverTypeTag, const TArray<FVector>& ProtectedDirections);

	/** Number of cover points currently indexed in the world. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Cover")
	int32 GetCoverPointCount() const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	// ---- Scoring tunables (no magic numbers in logic) ----

	/**
	 * Minimum dot between a candidate's protected directions and the threat direction for it to count as
	 * "facing the threat" in ProtectsAgainst. 1 = exact, 0 = perpendicular. Genre-neutral default.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Config, Category = "DesignPatterns|AI|Cover", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float ProtectionMinDot = 0.25f;

private:
	/** Self-register under DP.Service.AI.Cover (WeakObserved). */
	void RegisterSelfAsService();

	/** Resolve the GameInstance service locator, or null. */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/**
	 * Refresh the cover-point index from the world's live actors (so clients pick up replicated points and
	 * stale entries drop out). Const: mutates only the transient index.
	 */
	void RebuildCoverIndex() const;

	/** Score one cover point as cover from ThreatLocation for a seeker at Origin (higher is better, 0 = bad). */
	float ScorePoint(const AAI_CoverPoint& Point, const FVector& Origin, const FVector& ThreatLocation) const;

	/** Live cover points indexed by stable id; weak (the world owns them), rebuilt/pruned lazily. */
	UPROPERTY(Transient)
	TMap<FGuid, TWeakObjectPtr<AAI_CoverPoint>> Points;
};
