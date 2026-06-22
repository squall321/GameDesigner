// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Containers/Ticker.h"
#include "UObject/SoftObjectPtr.h"
#include "Seam/Audio_AudioController.h"
#include "VO/Audio_VOController.h"
#include "VO/Audio_VOTypes.h"
#include "Audio_VOSubsystem.generated.h"

class UAudio_VOBankDataAsset;

/**
 * VO/BARK (4) subsystem — RENAMED from "Dialogue" to avoid overlap with the Narrative conversation
 * domain. It owns a priority/interrupt VO QUEUE with per-bark cooldowns, plays 2D or spatialized lines
 * through the existing IAudio_AudioController seam (resolved from DP.Service.Audio — it never hard-
 * includes the concrete sound manager), and pushes a duck bus while a line plays so VO automatically
 * ducks music/SFX.
 *
 * It is CAPTION-AGNOSTIC: it plays sound only and never authors text. When a line starts and its
 * request carried an optional caption payload (an FLoc_SubtitleLine the producer built), the subsystem
 * re-broadcasts that opaque payload on DP.Bus.Loc.VoiceLine so the shipped bus-driven
 * ULoc_SubtitleSubsystem surfaces the subtitle — with NO dependency on the Localization module.
 *
 * SCOPE: GameInstance-scoped, LOCAL and COSMETIC; nothing here is replicated. Queue advance is driven
 * by an FTSTicker (registered in Initialize, removed in Deinitialize) plus each line's resolved
 * duration — the subsystem is not FTickable. It implements the module-local IAudio_VOController and
 * registers under DP.Service.Audio.VO (WeakObserved).
 */
UCLASS()
class DESIGNPATTERNSAUDIO_API UAudio_VOSubsystem
	: public UDP_GameInstanceSubsystem
	, public IAudio_VOController
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin IAudio_VOController
	virtual FGuid PlayVO_Implementation(const FAudio_VORequest& Request) override;
	virtual void StopVO_Implementation(FGuid Handle) override;
	virtual bool TryBark_Implementation(FGameplayTag BarkTag, FVector Location) override;
	//~ End IAudio_VOController

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Stop every VO line (active + queued) whose category matches Category (or a child). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio|VO")
	void StopAllVO(FGameplayTag Category);

	/** Register an additional VO bank at runtime (beyond settings defaults). Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio|VO")
	void AddVOBank(UAudio_VOBankDataAsset* Bank);

	/** Remove a previously-added VO bank. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio|VO")
	void RemoveVOBank(UAudio_VOBankDataAsset* Bank);

protected:
	/** VO is meaningless without a sound device; skip dedicated-server creation. */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

private:
	/** Loaded VO banks (settings defaults + runtime-added), in resolution order. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAudio_VOBankDataAsset>> LoadedBanks;

	/** The pending queue (FIFO within a priority; highest priority plays first). */
	UPROPERTY(Transient)
	TArray<FAudio_VOQueueEntry> Queue;

	/** The currently-playing line (invalid handle when nothing is playing). */
	UPROPERTY(Transient)
	FAudio_VOQueueEntry ActiveEntry;

	/** Per-bark cooldown gate: bark line tag -> earliest world time it may play again. */
	TMap<FGameplayTag, double> BarkCooldownUntil;

	/** Cached audio controller seam (resolved from the service locator; re-resolved if stale). */
	UPROPERTY(Transient)
	TScriptInterface<IAudio_AudioController> AudioController;

	/** Active duck handle pushed for the playing line (invalid when none). */
	FGuid ActiveDuckHandle;

	/** Monotonic enqueue sequence for tie-breaking. */
	int64 NextSequence = 1;

	/** True while a line is actively playing (ActiveEntry valid). */
	bool bPlaying = false;

	/** World time at which the active line is expected to finish (drives queue advance). */
	double ActiveFinishTime = 0.0;

	/** Whether this game instance has an audio device at all. */
	bool bAudioAvailable = false;

	/** FTSTicker handle for queue advancement / cooldown checks. */
	FTSTicker::FDelegateHandle TickerHandle;

	// ---- internals ----

	/** Resolve a VO entry by tag across loaded banks (first bank wins). */
	const FAudio_VOEntry* ResolveEntry(const FGameplayTag& LineTag) const;

	/** Resolve (and cache) the IAudio_AudioController seam. Null if unavailable. */
	IAudio_AudioController* ResolveAudioController();

	/** Insert an entry into the queue ordered by priority desc then sequence asc. */
	void EnqueueOrdered(FAudio_VOQueueEntry&& Entry);

	/** If nothing is playing, dequeue the best entry and start it. */
	void TryStartNext();

	/** Begin playing a resolved entry (loads sound, plays via controller, pushes duck, broadcasts caption). */
	void StartEntry(const FAudio_VOQueueEntry& Entry);

	/** Finish the active line: pop duck, clear state, then start the next queued line. */
	void FinishActive();

	/** Load the configured default VO banks from settings. */
	void LoadDefaultBanksFromSettings();

	/** Register/unregister this subsystem as the DP.Service.Audio.VO provider. */
	void RegisterAsService();
	void UnregisterAsService();

	/** Periodic: advance the queue when the active line's expected finish time passes. */
	bool Tick(float DeltaTime);

	/** Now in world seconds (real-time clock; VO is cosmetic and not sim-time gated). */
	double NowSeconds() const;
};
