// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "MessageBus/DPMessage.h"
#include "Tutorial/Tut_Condition.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Tut_HintSubsystem.generated.h"

class UTut_HintDefinition;

/**
 * Contextual, priority-queued, cooldowned hint system.
 *
 * GameInstance-scoped and LOCAL/COSMETIC. Registered hints (UTut_HintDefinition, resolved by DataTag from the
 * core data registry) are evaluated:
 *  - immediately when a bus event one of their TriggerEventTags matches arrives (event-driven hints), and
 *  - on a periodic cadence for purely world-hub-state-driven hints.
 *
 * When one or more hints are eligible (condition satisfied + per-hint cooldown elapsed + show-count not
 * exhausted + global cooldown elapsed), the highest-priority eligible hint surfaces as a HUD toast — by
 * broadcasting a notification request on the shared HUD notify channel (DP.Bus.HUD.Notify) so the HUD module
 * surfaces it WITHOUT this module depending on the HUD's concrete types. If no HUD listens, the broadcast is
 * an inert no-op. A FTut_HintEvent is also broadcast on DP.Bus.Tutorial.HintShown for telemetry mirrors.
 *
 * Implements ITut_ConditionContext so its hints' conditions read seen bus events + the world-hub seam through
 * the same decoupled contract the tutorial runner uses.
 *
 * Holds the world-hub read seam WEAKLY (resolved on demand from the locator) — never a hard cross-world ref.
 */
UCLASS()
class DESIGNPATTERNSTUTORIAL_API UTut_HintSubsystem
	: public UDP_GameInstanceSubsystem
	, public ITut_ConditionContext
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Public API ----

	/**
	 * Register a hint (by its UTut_HintDefinition DataTag) so the subsystem evaluates and may surface it.
	 * Idempotent. @return true if the hint was resolved + registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	bool RegisterHint(FGameplayTag HintTag);

	/** Stop evaluating a previously-registered hint. @return true if it was registered. */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	bool UnregisterHint(FGameplayTag HintTag);

	/** Clear all per-hint cooldowns and show counts (e.g. on a new game / context reset). */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void ResetHintState();

	/**
	 * Force an immediate evaluation pass over all registered hints (in addition to the event/periodic passes).
	 * Useful right after a context change a project knows about.
	 */
	UFUNCTION(BlueprintCallable, Category = "Tutorial|Hint")
	void EvaluateHintsNow();

	//~ Begin ITut_ConditionContext
	virtual bool HasSeenBusEvent(const FGameplayTag& EventTag) const override;
	virtual bool QueryHubValue(const FGameplayTag& Key, FInstancedStruct& Out) const override;
	//~ End ITut_ConditionContext

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Per-hint runtime bookkeeping (cooldown timestamp + how many times it has surfaced). */
	struct FHintRuntime
	{
		/** The resolved definition (held strong via the UPROPERTY map below; mirrored here for fast access). */
		TWeakObjectPtr<UTut_HintDefinition> Definition;

		/** World time (seconds) this hint last surfaced; negative means "never". */
		double LastShownTime = -1.0;

		/** Number of times this hint has surfaced this session. */
		int32 ShowCount = 0;
	};

	/** Subscribe the bus listener used to feed condition evaluation + event-driven hints. */
	void RegisterBusListeners();

	/** Handle a bus message: record the seen event and evaluate hints that react to its channel. */
	void HandleBusMessage(const FDP_Message& Message);

	/** Periodic ticker callback: evaluate hub-state-driven hints. */
	bool TickEvaluate(float DeltaTime);

	/**
	 * Evaluate the candidate hints (or all when Candidates is empty), pick the highest-priority eligible one
	 * respecting the global cooldown, and surface it.
	 */
	void EvaluateAndSurface(const TArray<FGameplayTag>& Candidates);

	/** True if a hint is currently eligible to surface (condition met + cooldowns + show-count). */
	bool IsHintEligible(FGameplayTag HintTag, const FHintRuntime& Runtime, double NowSeconds) const;

	/** Surface a hint: broadcast the HUD notify request + the HintShown event, update cooldown/show count. */
	void SurfaceHint(FGameplayTag HintTag, FHintRuntime& Runtime);

	/** Resolve the IWorldHub_Queryable provider object from the service locator, or null. */
	UObject* ResolveWorldHub() const;

	/** Current world time in seconds (falls back to FPlatformTime if no world is available). */
	double GetNowSeconds() const;

	/** Apply verbose-logging + cadence from settings; (re)installs the periodic ticker. */
	void ApplySettings();

	/** The registered hint definitions, keyed by DataTag, kept alive strongly while registered. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UTut_HintDefinition>> RegisteredHints;

	/** Per-hint runtime state, keyed by DataTag (parallel to RegisteredHints). */
	TMap<FGameplayTag, FHintRuntime> HintRuntime;

	/**
	 * Seen one-shot bus event channels (cleared periodically so an event only counts for a short window).
	 * Drives UTut_Condition_BusEvent for hint conditions.
	 */
	FGameplayTagContainer SeenEventTags;

	/** World time the most recent hint surfaced (for the global inter-hint cooldown); negative = never. */
	double LastAnyHintShownTime = -1.0;

	/** Periodic evaluation cadence (seconds), from settings. */
	float EvaluationInterval = 1.f;

	/** FTSTicker handle for the periodic evaluation pass. */
	FTSTicker::FDelegateHandle TickerHandle;
};
