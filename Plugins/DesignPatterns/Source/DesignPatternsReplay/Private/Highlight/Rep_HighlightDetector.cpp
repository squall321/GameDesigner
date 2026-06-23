// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Highlight/Rep_HighlightDetector.h"
#include "Highlight/Rep_HighlightRuleSet.h"
#include "Timeline/Rep_ReplayTimeline.h"
#include "Timeline/Rep_ReplayEvent.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "DesignPatternsReplayModule.h"
#include "Core/DPLog.h"

void URep_HighlightDetector::Initialize(URep_ReplayTimeline* InTimeline, URep_HighlightRuleSet* InRuleSet)
{
	// Rebind cleanly if already wired.
	Shutdown();

	Timeline = InTimeline;
	RuleSet = InRuleSet;

	if (Timeline.IsValid())
	{
		// Bind the dynamic delegate. AddDynamic requires a UFUNCTION target (HandleTimelineEvent).
		Timeline->OnTimelineEventRecorded.AddDynamic(this, &URep_HighlightDetector::HandleTimelineEvent);
	}
}

void URep_HighlightDetector::Shutdown()
{
	if (Timeline.IsValid())
	{
		Timeline->OnTimelineEventRecorded.RemoveDynamic(this, &URep_HighlightDetector::HandleTimelineEvent);
	}
	Windows.Reset();
	Timeline = nullptr;
}

void URep_HighlightDetector::Reset()
{
	Moments.Reset();
	Windows.Reset();
}

void URep_HighlightDetector::SweepExistingEvents()
{
	if (!Timeline.IsValid())
	{
		return;
	}

	// Reset windows so a sweep is deterministic regardless of any live state.
	Windows.Reset();

	const TArray<FRep_ReplayEvent>& Events = Timeline->GetEvents();
	for (const FRep_ReplayEvent& Event : Events)
	{
		IngestEvent(Event);
	}
}

void URep_HighlightDetector::HandleTimelineEvent(const FRep_ReplayEvent& Event)
{
	IngestEvent(Event);
}

void URep_HighlightDetector::IngestEvent(const FRep_ReplayEvent& Event)
{
	if (!Event.IsValid() || !RuleSet)
	{
		return;
	}

	// Never re-ingest our own promoted markers (distinct Rep.Highlight.* root) — that would re-promote.
	if (Event.EventTag.MatchesTag(Rep_NativeTags::Highlight))
	{
		return;
	}

	const FRep_HighlightRule* Rule = RuleSet->FindRuleForEvent(Event.EventTag);
	if (!Rule || !Rule->IsUsable())
	{
		return;
	}

	// Update the sliding window for this rule's trigger tag.
	FRuleWindow& Window = Windows.FindOrAdd(Rule->TriggerTag);
	Window.KindTag = Rule->KindTag;

	// Drop events older than the rule window relative to this event's time.
	const float WindowStart = Event.Time - Rule->WindowSeconds;
	Window.RecentTimes.RemoveAll([WindowStart](float T) { return T < WindowStart; });

	Window.RecentTimes.Add(Event.Time);

	// Accumulate magnitude (only int/float variants contribute numerically; others count as 1).
	double Contribution = 1.0;
	switch (Event.Payload.Type)
	{
	case ESeam_NetValueType::Int:   Contribution = (double)Event.Payload.IntValue; break;
	case ESeam_NetValueType::Float: Contribution = Event.Payload.FloatValue;       break;
	default: break;
	}
	Window.SummedMagnitude += Contribution;

	// Best-effort focus attribution: a vector/tag payload is not an entity, so attribution comes from
	// the killcam/death path. Here we leave LastFocus as-is unless a future event carries an entity.

	// Fire when the window holds enough events.
	if (Window.RecentTimes.Num() >= Rule->MinEventCount)
	{
		EmitMoment(*Rule, Window, Event.Time);

		// Reset this window so the same cluster does not re-fire on the very next event; a fresh
		// cluster must re-accumulate from scratch.
		Window.RecentTimes.Reset();
		Window.SummedMagnitude = 0.0;
	}
}

void URep_HighlightDetector::EmitMoment(const FRep_HighlightRule& Rule, const FRuleWindow& Window, float AnchorTime)
{
	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	const float SettingsLeadIn  = Settings ? Settings->HighlightClipLeadInSeconds  : 0.f;
	const float SettingsLeadOut = Settings ? Settings->HighlightClipLeadOutSeconds : 0.f;

	const float LeadIn  = RuleSet ? RuleSet->GetEffectiveLeadIn(SettingsLeadIn)   : FMath::Max(0.f, SettingsLeadIn);
	const float LeadOut = RuleSet ? RuleSet->GetEffectiveLeadOut(SettingsLeadOut) : FMath::Max(0.f, SettingsLeadOut);

	FRep_HighlightMoment Moment;
	Moment.MomentId = FSeam_EntityId::NewId();
	Moment.KindTag = Rule.KindTag;
	Moment.FocusEntity = Window.LastFocus;
	Moment.AnchorTimeSeconds = AnchorTime;
	Moment.InTimeSeconds = FMath::Max(0.f, AnchorTime - LeadIn);
	Moment.OutTimeSeconds = AnchorTime + LeadOut;
	Moment.ContributingEventCount = Window.RecentTimes.Num();
	Moment.Score = Rule.BaseScore + Rule.PerEventScore * FMath::Max(0, Window.RecentTimes.Num() - 1);
	Moment.Magnitude = FSeam_NetValue::MakeFloat(Window.SummedMagnitude);

	// Honour the rule-set's retain threshold; below it, do not surface the moment.
	const float MinScore = RuleSet ? RuleSet->MinScoreToRetain : 0.f;
	if (Moment.Score < MinScore)
	{
		return;
	}

	Moments.Add(Moment);

	// Keep newest-anchor first for UI convenience.
	Moments.Sort([](const FRep_HighlightMoment& A, const FRep_HighlightMoment& B)
	{
		return A.AnchorTimeSeconds > B.AnchorTimeSeconds;
	});

	// Record a marker back onto the timeline under the DISTINCT Rep.Highlight.* root, with the provided
	// time so it lands exactly at the anchor (and so the detector's MatchesTag guard skips it on ingest).
	if (Timeline.IsValid())
	{
		FRep_ReplayEvent Marker(AnchorTime, Rule.KindTag, Moment.DisplayLabel, Moment.Magnitude);
		Timeline->RecordEvent(Marker, /*bUseProvidedTime*/ true);
	}

	UE_LOG(LogDP, Verbose, TEXT("Highlight detected: kind=%s score=%.2f anchor=%.2f events=%d"),
		*Rule.KindTag.ToString(), Moment.Score, AnchorTime, Moment.ContributingEventCount);

	OnHighlightDetected.Broadcast(Moment);
}

bool URep_HighlightDetector::FindMoment(const FSeam_EntityId& MomentId, FRep_HighlightMoment& OutMoment) const
{
	for (const FRep_HighlightMoment& M : Moments)
	{
		if (M.MomentId == MomentId)
		{
			OutMoment = M;
			return true;
		}
	}
	return false;
}

void URep_HighlightDetector::EnforceRetainCap(int32 MaxCount)
{
	if (MaxCount <= 0 || Moments.Num() <= MaxCount)
	{
		return;
	}

	// Rank a transient copy by score (highest first), keep the top MaxCount, then restore anchor order.
	TArray<FRep_HighlightMoment> ByScore = Moments;
	ByScore.Sort(); // operator< sorts higher score first
	ByScore.SetNum(MaxCount);

	ByScore.Sort([](const FRep_HighlightMoment& A, const FRep_HighlightMoment& B)
	{
		return A.AnchorTimeSeconds > B.AnchorTimeSeconds;
	});
	Moments = MoveTemp(ByScore);
}
