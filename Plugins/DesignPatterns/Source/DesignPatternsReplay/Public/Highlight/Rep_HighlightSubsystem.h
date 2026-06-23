// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Seam/Rep_ReplayEventSource.h"
#include "Highlight/Rep_HighlightTypes.h"
#include "Tickable.h"
#include "UObject/WeakInterfacePtr.h"
#include "Rep_HighlightSubsystem.generated.h"

class URep_ReplaySubsystem;
class URep_HighlightDetector;
class URep_ClipController;
class URep_HighlightRuleSet;
class URep_PlaybackController;
class ISeam_AnalyticsSink;
struct FRep_ReplayEvent;

/** Fired after the retained moment set changes (a detect or a cap-enforced drop). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRep_OnHighlightsChanged);

/**
 * URep_HighlightSubsystem — the clip / highlight / reel system for replays.
 *
 * Responsibilities:
 *  - Owns a URep_HighlightDetector (auto-detects highlight moments from tagged timeline events via the
 *    timeline's OnTimelineEventRecorded, scored by a URep_HighlightRuleSet), and owns a
 *    URep_ClipController to play a single moment's window over the demo.
 *  - Implements IRep_ReplayEventSource and registers itself with the replay subsystem (and under the
 *    Service_Replay_Highlights locator key), so the timeline can poll its known markers when recording
 *    begins (and so a project can resolve the highlight system to drive clips/reels).
 *  - Forwards each detected highlight to the analytics sink (ISeam_AnalyticsSink) as a PII-safe
 *    aggregate event, when enabled — the cross-module highlight->analytics path the spec mandates.
 *  - Builds an exportable reel (FRep_HighlightReel) from the retained moments for the share service.
 *
 * LIFETIME / GC: a GameInstance subsystem. It holds the replay subsystem and world WEAKLY and
 * re-resolves on use (it outlives worlds); owned subobjects (detector, clip controller) are NewObject'd
 * with this as outer and held via UPROPERTY TObjectPtr. The analytics sink is held as a pruned-on-use
 * TWeakInterfacePtr. As an FTickableGameObject it fan-ticks the clip controller; ticking is gated so it
 * is inert unless a clip is playing.
 *
 * MP CAVEAT: highlight detection runs from the local timeline, which on the recording machine harvests
 * the authoritative event stream and on a playback machine harvests the loaded sidecar — so highlights
 * are consistent with whatever timeline this machine has. It is per-machine and never replicated.
 */
UCLASS()
class DESIGNPATTERNSREPLAY_API URep_HighlightSubsystem
	: public UDP_GameInstanceSubsystem
	, public IRep_ReplayEventSource
	, public FTickableGameObject
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Detection control ----

	/**
	 * Begin auto-detection for the current recording/playback timeline. Resolves the replay subsystem,
	 * loads the rule-set (from settings), wires the detector, and registers as an event source. Safe to
	 * call repeatedly. No-op if highlight detection is disabled in settings.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Highlight")
	void BeginDetection();

	/** Stop detection and unregister as an event source. Detected moments are retained until cleared. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Highlight")
	void EndDetection();

	/** Clear all detected highlight moments. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Highlight")
	void ClearHighlights();

	// ---- Query ----

	/** All retained highlight moments (newest-anchor first). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Highlight")
	TArray<FRep_HighlightMoment> GetHighlights() const;

	/** Number of retained moments. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Highlight")
	int32 GetHighlightCount() const;

	/** Build an exportable reel (ordered by score) from the retained moments for ReplayName. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Highlight")
	FRep_HighlightReel BuildReel(const FString& ReplayName, const FText& Title) const;

	// ---- Clip playback ----

	/**
	 * Play a single moment's clip window over the demo using the supplied playback controller. The
	 * subsystem fan-ticks the clip controller until the out-point. Returns false if the moment is
	 * unknown or playback is inactive.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Highlight")
	bool PlayHighlight(const FSeam_EntityId& MomentId, URep_PlaybackController* PlaybackController);

	/** The owned clip controller (always valid after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Highlight")
	URep_ClipController* GetClipController() const { return ClipController; }

	/** Broadcast after the retained moment set changes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Highlight")
	FRep_OnHighlightsChanged OnHighlightsChanged;

	//~ Begin IRep_ReplayEventSource
	virtual void GatherReplayEvents_Implementation(float RecordingTimeSeconds, TArray<FRep_ReplayEvent>& OutEvents) const override;
	virtual FGameplayTag GetEventSourceId_Implementation() const override;
	//~ End IRep_ReplayEventSource

	//~ Begin FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual UWorld* GetTickableGameObjectWorld() const override;
	//~ End FTickableGameObject

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** The owned detector (NewObject'd at Initialize, GC-owned via this UPROPERTY). */
	UPROPERTY(Transient)
	TObjectPtr<URep_HighlightDetector> Detector = nullptr;

	/** The owned clip controller. */
	UPROPERTY(Transient)
	TObjectPtr<URep_ClipController> ClipController = nullptr;

	/** The loaded rule-set (kept alive while detection is active). */
	UPROPERTY(Transient)
	TObjectPtr<URep_HighlightRuleSet> RuleSet = nullptr;

	/** The replay subsystem (weak: cross-world ref re-resolved on use). */
	TWeakObjectPtr<URep_ReplaySubsystem> ReplaySubsystem;

	/** The analytics sink seam (weak, pruned on use); resolved from the service locator. */
	TWeakInterfacePtr<ISeam_AnalyticsSink> AnalyticsSink;

	/** True while registered as an event source / detecting. */
	bool bDetecting = false;

	/** UFUNCTION bound to the detector's OnHighlightDetected. */
	UFUNCTION()
	void HandleHighlightDetected(const FRep_HighlightMoment& Moment);

	/** Resolve (and cache weakly) the replay subsystem for this game instance, or null. */
	URep_ReplaySubsystem* ResolveReplaySubsystem();

	/** Resolve the analytics sink from the locator (re-resolves if the cached weak ptr is dead). */
	ISeam_AnalyticsSink* ResolveAnalyticsSink();

	/** Forward a detected moment to analytics as a PII-safe aggregate event (when enabled). */
	void ForwardToAnalytics(const FRep_HighlightMoment& Moment);
};
