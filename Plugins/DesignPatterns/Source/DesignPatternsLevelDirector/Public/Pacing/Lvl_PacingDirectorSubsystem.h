// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UObject/ScriptInterface.h"
#include "UObject/WeakInterfacePtr.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "MessageBus/DPMessage.h"
#include "Lvl_PacingDirectorSubsystem.generated.h"

class ULvl_TensionCurveDataAsset;
class ISeam_EncounterDirector;

/**
 * AUTHORITY-ONLY tension-curve pacing director.
 *
 * For each active region it advances normalized encounter time on an FTSTicker cadence, samples the
 * region's ULvl_TensionCurveDataAsset into a 0..1 tension, maps that to a ProgressionInput, and drives
 * the world encounter director THROUGH the shared ISeam_EncounterDirector seam (resolved weakly from the
 * service locator under DP.Service.AI.EncounterDirector — no AI hard-include). Because that director
 * cannot mutate a live encounter's intensity (a single ProgressionInput is sampled at activation), the
 * pacing director RE-ACTIVATES only when tension crosses the curve's escalate/relax thresholds, with
 * hysteresis, and broadcasts DP.Bus.Lvl.Pacing.Escalated/.Relaxed on each crossing.
 *
 * It listens to DP.Bus.Lvl.Encounter.Activated/.Deactivated (from ULvl_EncounterActivatorComponent) to
 * auto-begin/end per-region pacing, but BeginPacing/EndPacing may also be driven directly.
 *
 * AUTHORITY: a single global pacing decision drives the (server-authoritative) encounter director, so
 * the whole subsystem is authority-only — every mutator early-returns on clients. The encounter state it
 * triggers replicates through the AI director's own actors, not through this subsystem.
 */
UCLASS()
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_PacingDirectorSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** True on server / standalone / listen-server host. ALL pacing gates on this at the TOP. */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * Begin pacing a region with the given tension curve (authority only). Re-beginning a region resets
	 * its clock and re-arms it. @return true if pacing started (false on client / null curve).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Pacing")
	bool BeginPacing(FGameplayTag RegionTag, ULvl_TensionCurveDataAsset* Curve);

	/**
	 * Stop pacing a region (authority only). Stops the encounter via the seam. @return true if a region
	 * was paced and stopped.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Pacing")
	bool EndPacing(FGameplayTag RegionTag);

	/** The current tension (0..1) of the most-recently-evaluated region (diagnostic). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Pacing")
	float GetCurrentTension() const { return LastTension; }

	/** Number of regions currently being paced. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Pacing")
	int32 GetActiveRegionCount() const { return PacedRegions.Num(); }

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** Which pacing band a region is currently in (drives re-activation on crossings). */
	enum class EPacingBand : uint8 { Relaxed, Mid, Escalated };

	/** Per-region pacing state. NOT replicated; authority-side only. */
	struct FPacedRegion
	{
		/** The tension curve driving this region. Strong-held via PacedCurves to keep it alive. */
		TWeakObjectPtr<ULvl_TensionCurveDataAsset> Curve;

		/** Elapsed encounter time (s). */
		float ElapsedSeconds = 0.f;

		/** Last tension sampled (0..1). */
		float LastTension = 0.f;

		/** Band the region last settled into (for hysteresis / crossing detection). */
		EPacingBand Band = EPacingBand::Mid;

		/** True once the encounter has been activated at least once for this region. */
		bool bActivated = false;
	};

	/**
	 * Resolve the encounter director seam from the locator (re-resolved each use; the adapter is a
	 * world-lifetime object so we never cache a strong ref). Returns a usable interface or empty.
	 */
	TScriptInterface<ISeam_EncounterDirector> ResolveEncounterDirector() const;

	/** FTSTicker callback advancing every paced region and applying band crossings. */
	bool TickPacing(float Dt);

	/** Advance one region by Dt, sample tension, and re-activate the encounter on a band crossing. */
	void StepRegion(FGameplayTag RegionTag, FPacedRegion& State, float Dt);

	/** Classify a tension into a band using the curve's hysteresis thresholds. */
	static EPacingBand ClassifyBand(float Tension, float RelaxThreshold, float EscalateThreshold,
		EPacingBand CurrentBand);

	/** Bus handler: an encounter activated for a region -> begin pacing it (if a curve is registered). */
	void HandleEncounterActivated(const FDP_Message& Message);

	/** Bus handler: an encounter deactivated for a region -> stop pacing it. */
	void HandleEncounterDeactivated(const FDP_Message& Message);

	/** Broadcast a pacing escalate/relax event on the bus. */
	void BroadcastPacingEvent(bool bEscalated, FGameplayTag RegionTag, FGameplayTag EncounterId,
		float Tension, float ProgressionInput) const;

	/** Active per-region pacing state, keyed by region tag. */
	TMap<FGameplayTag, FPacedRegion> PacedRegions;

	/**
	 * Strong references to the curves we are pacing with, so a curve passed transiently to BeginPacing
	 * (or resolved from a bus event) is not GC'd while in use. Cleared per-region on EndPacing.
	 */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<ULvl_TensionCurveDataAsset>> PacedCurves;

	/** Cached weak ref to the resolved encounter-director adapter (re-resolved on use). */
	mutable TWeakInterfacePtr<ISeam_EncounterDirector> CachedEncounterDirector;

	/** Tick handle for the cadence-driven pacing loop. Removed in Deinitialize. */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Bus listener handles, removed in Deinitialize. */
	FDP_ListenerHandle ActivatedListener;
	FDP_ListenerHandle DeactivatedListener;

	/** Last tension evaluated across all regions (diagnostic for GetCurrentTension). */
	float LastTension = 0.f;
};
