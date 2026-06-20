// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "UObject/WeakInterfacePtr.h"
#include "Seams/AI_SpawnParticipant.h"
#include "AI_SpawnDirectorSubsystem.generated.h"

class UAI_EncounterDataAsset;
class UAI_WaveDataAsset;
class UDP_ServiceLocatorSubsystem;
class UDP_SpawnFactorySubsystem;
class AActor;

/**
 * A single fallback spawn point, used when no ILvl_SpawnRegionProvider answers a region tag. Designers
 * register these on the director (RegisterFallbackPoint) so the module spawns sensibly without a level
 * integration. Pure value type.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_FallbackSpawnPoint
{
	GENERATED_BODY()

	/** The region tag this point answers for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Spawn")
	FGameplayTag RegionTag;

	/** World-space transform at the centre of the fallback region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|AI|Spawn")
	FTransform Transform = FTransform::Identity;

	FAI_FallbackSpawnPoint() = default;
};

/**
 * World-scoped, AUTHORITY-ONLY budget/wave spawn director.
 *
 * Wraps the core factory + pool to play an encounter asset: it gates activation on World-hub conditions
 * (IWorldHub_Queryable, resolved via the locator in the .cpp), then paces the encounter's waves by the
 * simulation clock (ISeam_SimClock, resolved from the locator) and the data-authored delays. Every
 * spawn goes through the core UDP_SpawnFactorySubsystem (resolved via FDP_SubsystemStatics), which itself
 * routes through the object pool when the recipe permits — so the director never hard-references the pool.
 *
 * Budget accounting: each spawned participant occupies budget (read from the wave entry / the participant
 * seam IAI_SpawnParticipant). The director tracks live participants as TWeakInterfacePtr and reclaims
 * budget as they die (IsAliveForBudget() flips false), firing DP.Bus.AI.Wave.Cleared when a wave's
 * budgeted participants are all gone. NOTHING here is replicated: this is server-authoritative spawning;
 * the spawned actors replicate themselves, and clients learn of wave events via the message bus
 * (re-published locally from already-replicated gameplay, per the cosmetic/authoritative split).
 */
UCLASS()
class DESIGNPATTERNSAI_API UAI_SpawnDirectorSubsystem : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own. True on server / standalone /
	 * listen-server host. All spawning gates on this at the TOP.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	// ---- Encounter control (AUTHORITY ONLY) ---------------------------------------------------

	/**
	 * Try to activate Encounter with the given progression input (sampled into budget/difficulty). If the
	 * encounter's hub gate conditions do not currently pass, activation is DEFERRED: the director keeps it
	 * pending and re-checks the gates each pacing tick until they pass (or StopEncounter is called).
	 * AUTHORITY ONLY. @return true if the encounter was accepted (activated or pended); false on a bad
	 * argument or on clients.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Spawn")
	bool ActivateEncounter(UAI_EncounterDataAsset* Encounter, float ProgressionInput);

	/**
	 * Stop the running (or pending) encounter, halting further spawns. Already-spawned actors are left
	 * alone (use the pool/their own lifecycle to clean up). AUTHORITY ONLY. @return true if one was running.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Spawn")
	bool StopEncounter();

	/** True while an encounter is activated (running its waves) or pending its gate. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Spawn")
	bool IsEncounterActive() const { return ActiveEncounter != nullptr; }

	// ---- Fallback spawn points (used when no region provider is registered) -------------------

	/** Register a fallback spawn point for a region tag. AUTHORITY-agnostic (pure config). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Spawn")
	void RegisterFallbackPoint(const FAI_FallbackSpawnPoint& Point);

	/** Remove every fallback point for a region tag. @return number removed. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Spawn")
	int32 ClearFallbackPoints(FGameplayTag RegionTag);

	// ---- Live state (reads) -------------------------------------------------------------------

	/** Live budget currently consumed by alive participants. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Spawn")
	int32 GetConsumedBudget() const { return ConsumedBudget; }

	/** Total budget available for the active encounter (0 when none active). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Spawn")
	int32 GetTotalBudget() const { return TotalBudget; }

	/** Number of director-tracked participants currently alive. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Spawn")
	int32 GetLiveParticipantCount() const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/** One alive participant the director is tracking for budget/clear accounting. */
	struct FTrackedParticipant
	{
		/**
		 * The participant's seam (weak), set when the spawned actor implements IAI_SpawnParticipant.
		 * When valid, aliveness comes from IsAliveForBudget(); a destroyed actor drops out on reconcile.
		 */
		TWeakInterfacePtr<IAI_SpawnParticipant> Participant;

		/**
		 * Weak back-reference to the spawned actor. Used as the aliveness signal for actors that do NOT
		 * implement the participant seam (alive == actor still valid and not pending-kill), so their
		 * budget is reclaimed exactly when the actor is destroyed rather than leaking or releasing early.
		 */
		TWeakObjectPtr<AActor> Actor;

		/** Budget this participant occupies (read once at spawn). */
		int32 BudgetCost = 0;

		/** Wave it was spawned for, for clear attribution. */
		FGameplayTag WaveTag;

		/** True when aliveness should be read from the participant seam; false to use Actor validity. */
		bool bHasSeam = false;
	};

	/** Per-entry runtime cursor while a wave is spawning (paces an entry's instances over time). */
	struct FEntryCursor
	{
		/** Index of the entry within the current wave's Entries array. */
		int32 EntryIndex = 0;

		/** How many of this entry's instances have spawned so far. */
		int32 Spawned = 0;

		/** Simulation time (encounter clock) at which the next instance of this entry may spawn. */
		double NextSpawnSimTime = 0.0;
	};

	/** Phase of the active encounter's wave playback. */
	enum class EWavePhase : uint8
	{
		/** No wave running; waiting for the next wave's lead-in to elapse. */
		Idle,
		/** A wave is actively spawning its entries. */
		Spawning,
		/** The wave finished spawning; waiting for it to clear (if bRequireClearBeforeNext). */
		AwaitingClear
	};

	// ---- Active-encounter state ----

	/** The encounter being played (null when none). Strong ref so it is not GC'd mid-run. */
	UPROPERTY(Transient)
	TObjectPtr<UAI_EncounterDataAsset> ActiveEncounter = nullptr;

	/** True while the encounter is pending its hub gate (not yet running waves). */
	bool bPendingGate = false;

	/** Progression input captured at ActivateEncounter, re-used for the next budget/difficulty sample. */
	float PendingProgressionInput = 0.f;

	/** Difficulty scalar sampled at activation (broadcast in payloads). */
	float ActiveDifficulty = 1.f;

	/** Total budget for the active encounter (base*difficulty + bonuses), clamped by the spawn cap. */
	int32 TotalBudget = 0;

	/** Budget currently consumed by alive participants. */
	int32 ConsumedBudget = 0;

	/** Index of the wave currently being played within the encounter's wave list. */
	int32 CurrentWaveIndex = 0;

	/** Loaded wave asset for CurrentWaveIndex (resolved from the soft pointer when the wave starts). */
	UPROPERTY(Transient)
	TObjectPtr<UAI_WaveDataAsset> CurrentWave = nullptr;

	/** Current wave-playback phase. */
	EWavePhase Phase = EWavePhase::Idle;

	/** Simulation-clock time (accumulated) used to pace delays. Advanced each tick by clock-scaled real dt. */
	double EncounterSimTime = 0.0;

	/** Sim time at which the current wave may start (after its lead-in). */
	double NextWaveStartSimTime = 0.0;

	/** Per-entry cursors for the wave currently spawning. */
	TArray<FEntryCursor> EntryCursors;

	/** All participants the director is tracking (alive accounting). */
	TArray<FTrackedParticipant> Tracked;

	/** Fallback spawn points keyed by region tag (used when no region provider answers). */
	TArray<FAI_FallbackSpawnPoint> FallbackPoints;

	/** Monotonic counter feeding the region provider's placement seed so spawns spread out. */
	int32 SpawnSeed = 0;

	// ---- Ticking ----

	/** FTSTicker handle for the pacing loop (the subsystem is not an FTickableGameObject). */
	FTSTicker::FDelegateHandle TickerHandle;

	/** Real-time accumulator so the director paces at DirectorTickHz, not every frame. */
	float TickAccumulator = 0.f;

	/** The FTSTicker callback: advances pacing and reconciles budget at the configured cadence. */
	bool TickDirector(float RealDeltaSeconds);

	/** One pacing step: advance sim time, run gates/waves, reconcile live budget. */
	void StepPacing();

	// ---- Encounter / wave playback ----

	/** Begin the active encounter's wave playback once its gate passes (samples budget/difficulty). */
	void BeginEncounterRun();

	/** Try to start the wave at CurrentWaveIndex; sets up entry cursors. Returns false if none remain. */
	bool StartCurrentWave();

	/** Spawn due entries for the current wave (paced, capped per tick by settings). */
	void TickWaveSpawning();

	/** Advance to the next wave (or complete/loop the encounter) once the current one is done/cleared. */
	void AdvanceWaveOrComplete();

	/** True if every budgeted participant from WaveTag is gone. */
	bool IsWaveCleared(const FGameplayTag& WaveTag) const;

	/** Drop dead participants, reclaiming their budget and firing wave-clear events. */
	void ReconcileTracked();

	// ---- Spawning primitives ----

	/**
	 * Spawn one participant for an entry of the current wave. Returns the spawned actor (or null). On
	 * success the actor is registered for budget tracking and ConsumedBudget is increased.
	 */
	AActor* SpawnOneFromEntry(const struct FAI_SpawnEntry& Entry, const FGameplayTag& WaveTag);

	/** Choose a world transform for a region tag (region provider first, then fallback points). */
	bool ResolveSpawnTransform(const FGameplayTag& RegionTag, FTransform& OutTransform);

	// ---- Gate / dependency resolution (all in .cpp; World/clock are private deps) --------------

	/** True if the active encounter's hub gate conditions currently all pass (or it has none). */
	bool AreGateConditionsMet() const;

	/** Resolve the service locator (GameInstance), or null. */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** Resolve the core spawn factory subsystem (World), or null. */
	UDP_SpawnFactorySubsystem* GetFactory() const;

	/** Current sim time scale from ISeam_SimClock (1 if no clock / paused returns 0). */
	double GetClockTimeScale() const;

	/** Self-register under DP.Service.AI.SpawnDirector (WeakObserved). */
	void RegisterSelfAsService();

	/** Reset all active-encounter state to idle (does not destroy spawned actors). */
	void ResetEncounterState();

	// ---- Bus helpers ----

	/** Broadcast a DP.Bus.AI.Wave.* event for a wave with a flat payload. */
	void BroadcastWaveEvent(const FGameplayTag& Channel, const UAI_WaveDataAsset& Wave, int32 WaveIndex,
		int32 PlannedCount, int32 SpawnedCount) const;

	/** Broadcast a DP.Bus.AI.Encounter.* event with a flat payload. */
	void BroadcastEncounterEvent(const FGameplayTag& Channel) const;
};
