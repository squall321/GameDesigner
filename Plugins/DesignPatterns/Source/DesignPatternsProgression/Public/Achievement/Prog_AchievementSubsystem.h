// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "MessageBus/DPMessage.h"
#include "Persist/Seam_Persistable.h"

// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4 and is merged into CoreUObject in
// 5.5+. Include the right header for the engine band BEFORE the generated header.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Prog_AchievementSubsystem.generated.h"

class UProg_AchievementDefinition;
class UDP_MessageBusSubsystem;

/**
 * Message-bus payload broadcast on DP.Bus.Prog.AchievementProgress and DP.Bus.Prog.AchievementUnlocked.
 *
 * Carried as the inner type of an FInstancedStruct so decoupled listeners (HUD toast, analytics overlay,
 * SFX) react to achievement state changes without binding to the subsystem. The same payload type serves
 * both channels: on the Unlocked channel Progress is always 1.0 and bUnlocked is true.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_AchievementEvent
{
	GENERATED_BODY()

	/** Identity tag of the achievement (a child of DP.Prog.Achievement). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Achievement")
	FGameplayTag Achievement;

	/** Normalized progress toward unlock [0,1]. 1.0 on the unlocked event. */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Achievement")
	float Progress = 0.f;

	/** True if this event represents a full unlock (always true on the Unlocked channel). */
	UPROPERTY(BlueprintReadOnly, Category = "Progression|Achievement")
	bool bUnlocked = false;

	FProg_AchievementEvent() = default;
};

/**
 * Durable save record for the achievement subsystem: the set of achievement identity tags that have been
 * unlocked. This is the ONLY thing the subsystem persists — conditions re-evaluate from live bus state on
 * load, so flags/counters are intentionally NOT saved (they rebuild as the session replays its events, and
 * an already-unlocked achievement never re-fires). Carried inside an FInstancedStruct via ISeam_Persistable.
 */
USTRUCT()
struct DESIGNPATTERNSPROGRESSION_API FProg_AchievementSaveRecord
{
	GENERATED_BODY()

	/** Identity tags of every unlocked achievement at save time. */
	UPROPERTY()
	TArray<FGameplayTag> UnlockedAchievements;

	FProg_AchievementSaveRecord() = default;
};

/**
 * Broadcast (C++/Blueprint) when an achievement unlocks on this peer.
 * @param Achievement The identity tag of the achievement that unlocked.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FProg_OnAchievementUnlocked, FGameplayTag, Achievement);

/**
 * Broadcast (C++/Blueprint) when an achievement's tracked progress fraction changes.
 * @param Achievement The identity tag whose progress changed.
 * @param Progress    The new normalized progress [0,1].
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FProg_OnAchievementProgress, FGameplayTag, Achievement, float, Progress);

/**
 * GameInstance subsystem that drives data-driven achievement unlocking — the Observer + Strategy patterns
 * wired through the framework's message bus and data registry.
 *
 * RESPONSIBILITIES
 *  - Builds a working set of UProg_AchievementDefinition assets from the data registry (every asset under
 *    the Prog_Achievement type), lazily on first relevant event.
 *  - Subscribes to the module's own DP.Bus.Prog root plus any project-configured trigger channels. On each
 *    matching message it (a) folds the message into its flag/counter/channel-hit accumulators — the state
 *    the UProg_Condition strategies read back — and (b) schedules a coalesced re-evaluation of every
 *    not-yet-unlocked achievement.
 *  - On a re-evaluation pass, an achievement whose EVERY condition Evaluate()s true is unlocked: the
 *    subsystem records it, fires the C++/BP delegates, broadcasts DP.Bus.Prog.AchievementUnlocked, records
 *    an aggregate event through ISeam_AnalyticsSink (resolved WEAKLY from the service locator), unlocks the
 *    mapped platform trophy through the optional ISeam_PlatformAchievements seam, and grants the
 *    definition's currency reward through ISeam_Wallet on the local player's wallet.
 *  - Persists the unlocked SET through ISeam_Persistable; RestoreState is authority-guarded by contract.
 *
 * AUTHORITY & REPLICATION: this subsystem holds NO replicated state (HARD RULE 5). It observes
 * already-replicated wallet balances and locally-rebroadcast bus traffic, and tracks unlock state locally
 * per peer — an achievement is a presentation/persistence concept, evaluated identically on each machine
 * from the same observed events. The only mutation that touches authoritative game state is the reward
 * grant, which goes through the wallet component's own HasAuthority() guard (a client grant is a no-op).
 *
 * SEAM DEGRADATION: every external seam is optional. No analytics sink => no analytics event; no platform
 * bridge => no trophy; no resolvable wallet => no reward; no data registry => an empty achievement set.
 * None of these is an error; each is a documented inert default, never a crash.
 */
UCLASS()
class DESIGNPATTERNSPROGRESSION_API UProg_AchievementSubsystem
	: public UDP_GameInstanceSubsystem
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Accumulator reads (used by UProg_Condition strategies) ----

	/** Current boolean flag value for FlagTag (false if never raised). Child-matching is NOT applied here. */
	bool GetHubFlag(const FGameplayTag& FlagTag) const;

	/** Current accumulated counter value for CounterTag (0 if never incremented). */
	int64 GetHubCounter(const FGameplayTag& CounterTag) const;

	/** Number of message occurrences seen on ChannelTag (exact channel, 0 if none). */
	int64 GetChannelHitCount(const FGameplayTag& ChannelTag) const;

	// ---- Authoritative-of-presentation accumulator writes (local, game thread) ----

	/**
	 * Raise (or, with bValue=false, clear) a named flag, then schedule a re-evaluation. Projects can drive
	 * the accumulator directly for state that isn't expressed as a bus payload field.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Achievement")
	void SetHubFlag(FGameplayTag FlagTag, bool bValue = true);

	/** Add Delta to a named counter (clamped at 0), then schedule a re-evaluation. */
	UFUNCTION(BlueprintCallable, Category = "Progression|Achievement")
	void AddHubCounter(FGameplayTag CounterTag, int64 Delta = 1);

	// ---- Unlock state / queries ----

	/** True if Achievement has been unlocked on this peer. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Progression|Achievement")
	bool IsUnlocked(FGameplayTag Achievement) const;

	/** Every unlocked achievement tag (snapshot). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Progression|Achievement")
	TArray<FGameplayTag> GetUnlockedAchievements() const;

	/** Current normalized progress [0,1] toward unlocking Achievement (1 if already unlocked, 0 if unknown). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Progression|Achievement")
	float GetAchievementProgress(FGameplayTag Achievement) const;

	/**
	 * Explicitly unlock Achievement, bypassing condition evaluation (story/scripted unlocks). Idempotent:
	 * a no-op if already unlocked or if Achievement isn't a known definition. Fires the same delegates,
	 * bus events, analytics, platform trophy and reward as an evaluated unlock. Returns true if it newly
	 * unlocked.
	 */
	UFUNCTION(BlueprintCallable, Category = "Progression|Achievement")
	bool ForceUnlock(FGameplayTag Achievement);

	/** Force a full re-evaluation of every not-yet-unlocked achievement right now (skips the coalesce timer). */
	UFUNCTION(BlueprintCallable, Category = "Progression|Achievement")
	void EvaluateNow();

	/** Clear all unlocked achievements + accumulators (new-game / debug). Re-broadcasts nothing. */
	UFUNCTION(BlueprintCallable, Category = "Progression|Achievement")
	void ResetAllProgress();

	// ---- Delegates ----

	/** Broadcast when an achievement unlocks on this peer. */
	UPROPERTY(BlueprintAssignable, Category = "Progression|Achievement")
	FProg_OnAchievementUnlocked OnAchievementUnlocked;

	/** Broadcast when an achievement's tracked progress fraction changes. */
	UPROPERTY(BlueprintAssignable, Category = "Progression|Achievement")
	FProg_OnAchievementProgress OnAchievementProgress;

	// ---- ISeam_Persistable ----

	/** Write the unlocked-achievement set into Out (game thread). */
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;

	/** Restore the unlocked-achievement set. AUTHORITY-guarded by contract; refires nothing. */
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;

	/** Stable kind tag (DP.Prog.Persist.Achievements) so a save can route the record back. */
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

protected:
	/**
	 * Authority check for a GameInstance-scoped subsystem. True when the local peer is the host (server,
	 * listen-server or standalone). Used only to gate RestoreState per the ISeam_Persistable contract.
	 * Returns true defensively when no world/net context exists yet (single-player / early load).
	 */
	bool HasHostAuthority() const;

private:
	// ---- Definition working set ----

	/** Resolved achievement definitions, keyed by identity tag. Lazily built from the data registry. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UProg_AchievementDefinition>> Definitions;

	/** Whether the definition set has been gathered this session. */
	bool bDefinitionsBuilt = false;

	/** Ensure Definitions is populated from the data registry (lazy, idempotent). */
	void EnsureDefinitionsBuilt();

	// ---- Bus subscription ----

	/** Resolve the message bus subsystem (null in early/CDO contexts). */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Subscribe to DP.Bus.Prog + each configured trigger channel. Idempotent. */
	void SubscribeToTriggers();

	/** Fold one incoming bus message into the accumulators, then schedule a re-evaluation. */
	void HandleBusMessage(const FDP_Message& Message);

	// ---- Accumulators (the state conditions read) ----

	/** Raised boolean flags (only "true" flags are stored; absence == false). */
	TSet<FGameplayTag> Flags;

	/** Named counters folded from payload deltas / explicit AddHubCounter. */
	TMap<FGameplayTag, int64> Counters;

	/** Per-channel raw message-occurrence counts (exact channel keying). */
	TMap<FGameplayTag, int64> ChannelHits;

	/** Set of achievement identity tags already unlocked on this peer. */
	TSet<FGameplayTag> Unlocked;

	/** Last reported progress per achievement, so we only fire the progress delegate/bus on a real change. */
	TMap<FGameplayTag, float> LastReportedProgress;

	// ---- Coalesced evaluation ----

	/** True while a re-evaluation is scheduled but not yet run (debounce flag). */
	bool bEvaluationScheduled = false;

	/** World time of the last full evaluation, for the MinEvaluationIntervalSeconds throttle. */
	double LastEvaluationTime = -BIG_NUMBER;

	/** Timer handle backing the coalesced re-evaluation. */
	FTimerHandle EvaluationTimerHandle;

	/** Request a re-evaluation, coalescing bursts per MinEvaluationIntervalSeconds. */
	void ScheduleEvaluation();

	/** Run a full evaluation pass over every not-yet-unlocked definition. */
	void RunEvaluation();

	/** Evaluate one definition; unlock it (and run all unlock side effects) if every condition passes. */
	void EvaluateDefinition(UProg_AchievementDefinition& Def);

	/** Recompute and, if changed, surface the progress fraction for Def (delegate + optional bus/platform). */
	void UpdateProgress(UProg_AchievementDefinition& Def);

	// ---- Unlock side effects ----

	/** Common unlock path: record + delegates + bus + analytics + platform trophy + reward. Idempotent. */
	void DoUnlock(UProg_AchievementDefinition& Def);

	/** Record an aggregate "achievement unlocked" event through the analytics seam (no-op if unbound). */
	void RecordUnlockAnalytics(const UProg_AchievementDefinition& Def) const;

	/** Unlock the platform trophy mapped to Def through the optional platform seam (no-op if unbound). */
	void ReportPlatformUnlock(const UProg_AchievementDefinition& Def) const;

	/** Report partial platform progress for Def if enabled in settings and the platform seam is bound. */
	void ReportPlatformProgress(const UProg_AchievementDefinition& Def, float Progress) const;

	/** Grant Def's currency reward through ISeam_Wallet on the local player's wallet (no-op if none). */
	void GrantReward(const UProg_AchievementDefinition& Def) const;

	/** Resolve the local player's wallet seam (off the player controller / pawn / state). Null if absent. */
	TScriptInterface<class ISeam_Wallet> ResolveLocalWallet() const;

	/** Broadcast a payload on Channel through the bus (no-op if the bus is unavailable). */
	void BroadcastEvent(const FGameplayTag& Channel, const FProg_AchievementEvent& Event) const;
};
