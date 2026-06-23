// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Highlight/Rep_HighlightTypes.h"
#include "Rep_HighlightDetector.generated.h"

class URep_ReplayTimeline;
class URep_HighlightRuleSet;
struct FRep_ReplayEvent;

/** Fired when the detector promotes a cluster into a new highlight moment. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRep_OnHighlightDetected, const FRep_HighlightMoment&, Moment);

/**
 * URep_HighlightDetector — turns clusters of tagged timeline events into scored highlight moments.
 *
 * INGEST: it has exactly ONE ingest path — the timeline's OnTimelineEventRecorded delegate (live,
 * while recording) plus a one-shot sweep of GetEvents() (when loaded for playback). It deliberately
 * does NOT re-subscribe to the message bus: the timeline already promotes bus broadcasts into
 * Rep.Event.* markers, so re-subscribing would double-count. To avoid re-promoting its own output it
 * ignores any event under the Rep.Highlight.* root (the detector writes promoted markers back to the
 * timeline under that distinct root via RecordEvent).
 *
 * DETECTION: for each incoming event it consults the rule-set (URep_HighlightRuleSet::FindRuleForEvent)
 * and maintains a small per-rule sliding window of recent matching event times; when the window holds
 * at least MinEventCount events it emits a FRep_HighlightMoment (scored from BaseScore + per-event),
 * records a Rep.Highlight.* marker back onto the timeline, and broadcasts OnHighlightDetected.
 *
 * Owned by URep_HighlightSubsystem (a UPROPERTY TObjectPtr); never standalone. All state is LOCAL —
 * detection runs on the recording/playback machine and never mutates replicated state.
 */
UCLASS()
class DESIGNPATTERNSREPLAY_API URep_HighlightDetector : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Wire the detector to its timeline and rule-set. Binds OnTimelineEventRecorded. Safe to call
	 * again to rebind (the previous binding is removed first).
	 */
	void Initialize(URep_ReplayTimeline* InTimeline, URep_HighlightRuleSet* InRuleSet);

	/** Unbind the timeline delegate and clear transient windows. Called by the subsystem at shutdown. */
	void Shutdown();

	/** Clear all detected moments and reset the sliding windows (e.g. when a new recording starts). */
	void Reset();

	/**
	 * Sweep the timeline's CURRENT event list once and detect any moments present (used after
	 * LoadForPlayback, where events already exist rather than arriving live). Idempotent: events that
	 * already produced a moment are not re-promoted.
	 */
	void SweepExistingEvents();

	/** All highlight moments detected so far, newest-anchor first. */
	const TArray<FRep_HighlightMoment>& GetMoments() const { return Moments; }

	/** Number of detected moments. */
	int32 GetMomentCount() const { return Moments.Num(); }

	/** Find a moment by its id. Returns false if not present. */
	bool FindMoment(const FSeam_EntityId& MomentId, FRep_HighlightMoment& OutMoment) const;

	/** Drop the lowest-scoring moments until at most MaxCount remain (0 => unbounded). */
	void EnforceRetainCap(int32 MaxCount);

	/** Broadcast when a new moment is detected. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Highlight")
	FRep_OnHighlightDetected OnHighlightDetected;

private:
	/** A per-rule sliding window of recent matching event times (demo-relative seconds). */
	struct FRuleWindow
	{
		/** The kind tag the rule stamps (so we can group windows by output kind). */
		FGameplayTag KindTag;

		/** Recent matching event times, pruned to the rule window on each insert. */
		TArray<float> RecentTimes;

		/** Summed magnitude of the events currently in the window (for the produced moment payload). */
		double SummedMagnitude = 0.0;

		/** The most recent focus entity contributing to the window (best-effort attribution). */
		FSeam_EntityId LastFocus;
	};

	/** Bound timeline (weak — the subsystem owns it; the detector only observes). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_ReplayTimeline> Timeline;

	/** Resolved rule-set (strong: a data asset the subsystem loaded and keeps alive via the settings ref). */
	UPROPERTY(Transient)
	TObjectPtr<URep_HighlightRuleSet> RuleSet;

	/** Detected moments (kept newest-anchor first). */
	UPROPERTY(Transient)
	TArray<FRep_HighlightMoment> Moments;

	/** Per-trigger-tag sliding windows. Keyed by the rule's TriggerTag. */
	TMap<FGameplayTag, FRuleWindow> Windows;

	/** Handle for the bound OnTimelineEventRecorded delegate (so we can unbind cleanly). */
	FDelegateHandle TimelineEventBinding;

	/** UFUNCTION bound to the timeline's dynamic delegate (must be a UFUNCTION). */
	UFUNCTION()
	void HandleTimelineEvent(const FRep_ReplayEvent& Event);

	/** Core ingest: evaluate one event against the rules, maybe producing a moment. */
	void IngestEvent(const FRep_ReplayEvent& Event);

	/** Produce, store, mark-back-to-timeline and broadcast a moment from a fired rule + window. */
	void EmitMoment(const struct FRep_HighlightRule& Rule, const FRuleWindow& Window, float AnchorTime);
};
