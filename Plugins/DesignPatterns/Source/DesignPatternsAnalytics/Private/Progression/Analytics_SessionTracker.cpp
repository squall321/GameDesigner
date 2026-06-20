// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Progression/Analytics_SessionTracker.h"
#include "Experiment/Analytics_ExperimentTags.h"
#include "Subsystem/Analytics_Subsystem.h"

#include "Core/DPLog.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Net/Seam_NetValue.h"
#include "Analytics/Seam_AnalyticsSink.h"

#include "HAL/PlatformTime.h"

namespace
{
	const FName GAttr_Reason(TEXT("reason"));
	const FName GAttr_SessionSeconds(TEXT("session_seconds"));
	const FName GAttr_ForegroundSeconds(TEXT("foreground_seconds"));
	const FName GAttr_SuspendCount(TEXT("suspend_count"));
	const FName GAttr_SummaryIndex(TEXT("summary_index"));

	/** Reason tags reuse the module's own event/bus tags so dashboards can group on them. */
}

void UAnalytics_SessionTracker::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const double Now = FPlatformTime::Seconds();
	SessionStartTime = Now;
	ForegroundSegmentStart = Now;
	AccumulatedForegroundSeconds = 0.0;
	bSuspended = false;
	SuspendCount = 0;
	SummaryEmitCount = 0;
	bSummaryEmittedForCurrentState = false;

	MessageBus = GetGameInstance() ? GetGameInstance()->GetSubsystem<UDP_MessageBusSubsystem>() : nullptr;
	SubscribeToAppLifecycleBus();

	UE_LOG(LogDP, Log, TEXT("Analytics SessionTracker initialized; session clock started."));
}

void UAnalytics_SessionTracker::Deinitialize()
{
	// Backstop summary on a clean shutdown that never received an OS suspend signal (e.g. desktop
	// quit). Avoid a duplicate if we already emitted for the current (suspended) state.
	if (!bSummaryEmittedForCurrentState)
	{
		EmitSessionSummary(AnalyticsProgressionTags::Event_Session_Summary);
	}

	if (MessageBus)
	{
		if (SuspendListenerHandle.IsValid())
		{
			MessageBus->StopListening(SuspendListenerHandle);
		}
		if (ResumeListenerHandle.IsValid())
		{
			MessageBus->StopListening(ResumeListenerHandle);
		}
		// Defensive: also drop anything else we might own on the bus.
		MessageBus->StopListeningForOwner(this);
	}
	SuspendListenerHandle = FDP_ListenerHandle();
	ResumeListenerHandle = FDP_ListenerHandle();
	MessageBus = nullptr;
	CachedAnalyticsSubsystem.Reset();

	Super::Deinitialize();
}

void UAnalytics_SessionTracker::SubscribeToAppLifecycleBus()
{
	if (!MessageBus)
	{
		// No bus -> no suspend bridge. The shutdown backstop in Deinitialize still produces a summary.
		UE_LOG(LogDP, Verbose, TEXT("SessionTracker: no message bus; relying on shutdown summary only."));
		return;
	}

	// Channels are configurable via the module's own tags (defaults documented in the tag file). We
	// subscribe by tag and never include the Platform module — the host bridges the OS signal here.
	const FGameplayTag SuspendChannel = AnalyticsProgressionTags::Bus_App_Suspend;
	const FGameplayTag ResumeChannel = AnalyticsProgressionTags::Bus_App_Resume;

	if (SuspendChannel.IsValid())
	{
		SuspendListenerHandle = MessageBus->ListenNative(
			SuspendChannel,
			[this](const FDP_Message& Msg) { HandleAppSuspend(Msg); },
			this,
			EDP_MessageMatch::ExactOrChild);
	}

	if (ResumeChannel.IsValid())
	{
		ResumeListenerHandle = MessageBus->ListenNative(
			ResumeChannel,
			[this](const FDP_Message& Msg) { HandleAppResume(Msg); },
			this,
			EDP_MessageMatch::ExactOrChild);
	}
}

UAnalytics_Subsystem* UAnalytics_SessionTracker::ResolveAnalyticsSubsystem()
{
	if (UAnalytics_Subsystem* Cached = CachedAnalyticsSubsystem.Get())
	{
		return Cached;
	}
	UAnalytics_Subsystem* Sub = GetGameInstance() ? GetGameInstance()->GetSubsystem<UAnalytics_Subsystem>() : nullptr;
	CachedAnalyticsSubsystem = Sub;
	return Sub;
}

void UAnalytics_SessionTracker::CloseForegroundSegment(double Now)
{
	if (!bSuspended)
	{
		AccumulatedForegroundSeconds += FMath::Max(0.0, Now - ForegroundSegmentStart);
	}
}

double UAnalytics_SessionTracker::GetSessionDurationSeconds() const
{
	return FMath::Max(0.0, FPlatformTime::Seconds() - SessionStartTime);
}

double UAnalytics_SessionTracker::GetForegroundPlaytimeSeconds() const
{
	double Total = AccumulatedForegroundSeconds;
	if (!bSuspended)
	{
		// Add the currently-open foreground segment.
		Total += FMath::Max(0.0, FPlatformTime::Seconds() - ForegroundSegmentStart);
	}
	return Total;
}

void UAnalytics_SessionTracker::HandleAppSuspend(const FDP_Message& Message)
{
	if (bSuspended)
	{
		// Already suspended; ignore a duplicate signal.
		return;
	}

	const double Now = FPlatformTime::Seconds();
	CloseForegroundSegment(Now);
	bSuspended = true;
	++SuspendCount;

	UE_LOG(LogDP, Verbose, TEXT("SessionTracker: app suspended (count=%d); emitting summary."), SuspendCount);

	// Emit the summary now: on mobile, the app may be killed while suspended, so suspend is our last
	// reliable chance to report. Use the suspend bus channel as the reason for groupability.
	EmitSessionSummary(AnalyticsProgressionTags::Bus_App_Suspend);
	bSummaryEmittedForCurrentState = true;
}

void UAnalytics_SessionTracker::HandleAppResume(const FDP_Message& Message)
{
	if (!bSuspended)
	{
		return;
	}
	bSuspended = false;
	bSummaryEmittedForCurrentState = false;
	ForegroundSegmentStart = FPlatformTime::Seconds();

	UE_LOG(LogDP, Verbose, TEXT("SessionTracker: app resumed; new foreground segment started."));
}

void UAnalytics_SessionTracker::EmitSessionSummary(FGameplayTag SummaryReason)
{
	UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem();
	if (!Analytics)
	{
		// No subsystem -> nothing to record. Still bump the counter for debug parity? No: only count
		// summaries that were actually handed to the (consent-gated) subsystem.
		return;
	}

	const double SessionSeconds = GetSessionDurationSeconds();
	const double ForegroundSeconds = GetForegroundPlaytimeSeconds();

	TArray<FSeam_AnalyticsAttr> Attrs;
	Attrs.Reserve(5);
	if (SummaryReason.IsValid())
	{
		Attrs.Emplace(GAttr_Reason, FSeam_NetValue::MakeTag(SummaryReason));
	}
	Attrs.Emplace(GAttr_SessionSeconds, FSeam_NetValue::MakeFloat(SessionSeconds));
	Attrs.Emplace(GAttr_ForegroundSeconds, FSeam_NetValue::MakeFloat(ForegroundSeconds));
	Attrs.Emplace(GAttr_SuspendCount, FSeam_NetValue::MakeInt(SuspendCount));
	Attrs.Emplace(GAttr_SummaryIndex, FSeam_NetValue::MakeInt(SummaryEmitCount));

	Analytics->RecordEvent(AnalyticsProgressionTags::Event_Session_Summary, Attrs);

	// Flush immediately: a suspend summary must reach the sink before the OS may freeze/kill us.
	Analytics->Flush();

	++SummaryEmitCount;

	UE_LOG(LogDP, Verbose, TEXT("SessionTracker: summary #%d emitted (session=%.1fs, fg=%.1fs, suspends=%d)."),
		SummaryEmitCount, SessionSeconds, ForegroundSeconds, SuspendCount);
}

FString UAnalytics_SessionTracker::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Session: %.0fs (fg %.0fs), suspended=%s, suspends=%d, summaries=%d"),
		GetSessionDurationSeconds(),
		GetForegroundPlaytimeSeconds(),
		bSuspended ? TEXT("yes") : TEXT("no"),
		SuspendCount,
		SummaryEmitCount);
}
