// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Subsystem/Analytics_Subsystem.h"

#include "DesignPatternsAnalyticsModule.h"
#include "Settings/Analytics_DeveloperSettings.h"
#include "Event/Analytics_EventMapDataAsset.h"
#include "Seam/Analytics_PlayerIdProvider.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Analytics/Seam_AnalyticsSink.h"

#include "Async/Async.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/DateTime.h"
#include "Misc/App.h"
#include "HAL/PlatformProcess.h"
#include "Math/UnrealMathUtility.h"
#include "UObject/ScriptInterface.h"

namespace
{
	/** Map a net value type to a short JSON-friendly tag for the file sink. */
	FString NetValueToJson(const FSeam_NetValue& Value)
	{
		switch (Value.Type)
		{
		case ESeam_NetValueType::Bool:   return Value.bValue ? TEXT("true") : TEXT("false");
		case ESeam_NetValueType::Int:    return FString::Printf(TEXT("%lld"), Value.IntValue);
		case ESeam_NetValueType::Float:  return FString::Printf(TEXT("%f"), Value.FloatValue);
		case ESeam_NetValueType::Vector: return FString::Printf(TEXT("\"%s\""), *Value.VectorValue.ToString());
		case ESeam_NetValueType::Tag:    return FString::Printf(TEXT("\"%s\""), *Value.TagValue.ToString());
		case ESeam_NetValueType::Name:   return FString::Printf(TEXT("\"%s\""), *Value.NameValue.ToString());
		default:                         return TEXT("null");
		}
	}
}

void UAnalytics_Subsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Consent starts OFF when the project keeps the default-off posture (the recommended,
	// privacy-safe behaviour). A project that opts out of default-off starts granted.
	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	const bool bDefaultOff = (Settings ? Settings->bConsentDefaultOff : true /*defensive fallback: privacy-safe*/);
	bConsentGranted = !bDefaultOff;

	// Per-session salt for experiment bucketing fallback (when no stable id is available).
	SessionSalt = FMath::Rand() ^ static_cast<uint32>(FDateTime::UtcNow().GetTicks());

	// Cache the GI-scoped message bus (a sibling subsystem; valid for the GI lifetime).
	MessageBus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	SubscribeToBus();

	// Periodic flush timer (skipped when the interval is non-positive).
	const float Interval = Settings ? Settings->GetEffectiveFlushInterval() : 30.f /*defensive fallback*/;
	if (Interval > 0.f)
	{
		FlushTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UAnalytics_Subsystem::TickFlush), Interval);
	}

	// If consent is already granted at startup, emit SessionStart immediately.
	if (bConsentGranted)
	{
		RecordSimpleEvent(AnalyticsNativeTags::Analytics_Event_SessionStart);
		bSessionStartRecorded = true;
	}

	UE_LOG(LogDP, Log, TEXT("Analytics subsystem initialized (consent=%s)."),
		bConsentGranted ? TEXT("granted") : TEXT("off"));
}

void UAnalytics_Subsystem::Deinitialize()
{
	// Final flush so a clean shutdown does not drop buffered telemetry.
	if (bConsentGranted)
	{
		RecordSimpleEvent(AnalyticsNativeTags::Analytics_Event_SessionEnd);
		FlushInternal();
	}

	if (FlushTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(FlushTickerHandle);
		FlushTickerHandle.Reset();
	}

	if (MessageBus && BusListenerHandle.IsValid())
	{
		MessageBus->StopListeningForOwner(this);
		BusListenerHandle = FDP_ListenerHandle();
	}

	SinkObjectWeak.Reset();
	SinkInterface = nullptr;
	MessageBus = nullptr;
	EventMap = nullptr;

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------------------------
// Consent
// ---------------------------------------------------------------------------------------------

void UAnalytics_Subsystem::SetConsent(bool bGranted)
{
	if (bGranted == bConsentGranted)
	{
		return;
	}

	bConsentGranted = bGranted;

	if (bConsentGranted)
	{
		UE_LOG(LogDP, Log, TEXT("Analytics consent GRANTED."));
		if (!bSessionStartRecorded)
		{
			RecordSimpleEvent(AnalyticsNativeTags::Analytics_Event_SessionStart);
			bSessionStartRecorded = true;
		}
	}
	else
	{
		// Revoking consent forgets pending data: clear the buffer without flushing.
		UE_LOG(LogDP, Log, TEXT("Analytics consent REVOKED; discarding %d buffered event(s)."),
			EventBuffer.Num());
		EventBuffer.Reset();
		bSessionStartRecorded = false;
	}
}

// ---------------------------------------------------------------------------------------------
// Recording
// ---------------------------------------------------------------------------------------------

void UAnalytics_Subsystem::RecordEvent(FGameplayTag EventTag, const TArray<FSeam_AnalyticsAttr>& Attributes)
{
	// Consent gate: record NOTHING until consent is granted.
	if (!bConsentGranted)
	{
		return;
	}

	if (!EventTag.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("Analytics RecordEvent ignored: invalid event tag."));
		return;
	}

	FAnalytics_BufferedEvent& Buffered = EventBuffer.AddDefaulted_GetRef();
	Buffered.EventTag = EventTag;
	Buffered.Attributes = Attributes; // value copy; attributes are PII-safe FSeam_NetValue
	Buffered.TimestampSeconds = FApp::GetCurrentTime();

	// Additive in-process observer hook (game thread). Fired AFTER the consent gate so observers
	// (funnel/heatmap/breadcrumb/dashboard) never see telemetry recorded without consent. The
	// payload mirrors the just-buffered event; broadcast by const ref to the original arguments to
	// avoid an extra copy. EnforceBufferCap below may drop OLDER events, but the event we just
	// broadcast is the newest and is unaffected.
	OnEventRecorded.Broadcast(EventTag, Attributes);

	EnforceBufferCap();

	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	const int32 Threshold = Settings ? Settings->GetEffectiveBatchSizeThreshold() : 64 /*defensive fallback*/;
	if (EventBuffer.Num() >= Threshold)
	{
		FlushInternal();
	}
}

void UAnalytics_Subsystem::RecordSimpleEvent(FGameplayTag EventTag)
{
	static const TArray<FSeam_AnalyticsAttr> Empty;
	RecordEvent(EventTag, Empty);
}

void UAnalytics_Subsystem::Flush()
{
	FlushInternal();
}

void UAnalytics_Subsystem::EnforceBufferCap()
{
	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	const int32 Cap = Settings ? Settings->GetEffectiveMaxBufferedEvents() : 4096 /*defensive fallback*/;
	if (EventBuffer.Num() <= Cap)
	{
		return;
	}

	const int32 Overflow = EventBuffer.Num() - Cap;
	UE_LOG(LogDP, Warning, TEXT("Analytics buffer overflow: dropping %d oldest event(s) (cap %d)."),
		Overflow, Cap);
	EventBuffer.RemoveAt(0, Overflow, /*bAllowShrinking*/ false);
}

// ---------------------------------------------------------------------------------------------
// Sink resolution (weak-held, pruned)
// ---------------------------------------------------------------------------------------------

void UAnalytics_Subsystem::RefreshSink()
{
	SinkObjectWeak.Reset();
	SinkInterface = nullptr;

	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	if (!Settings || !Settings->AnalyticsSinkServiceTag.IsValid())
	{
		return;
	}

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}

	UObject* Provider = Locator->ResolveService(Settings->AnalyticsSinkServiceTag);
	if (!Provider)
	{
		return;
	}

	// Only accept a provider that implements the sink seam.
	if (Provider->GetClass()->ImplementsInterface(USeam_AnalyticsSink::StaticClass()))
	{
		SinkObjectWeak = Provider;
		SinkInterface = Cast<ISeam_AnalyticsSink>(Provider);
		// Cast can be null for a BP-only interface implementation; we then route via Execute_.
	}
}

bool UAnalytics_Subsystem::HasLiveSink()
{
	// Prune a dead weak ref before deciding.
	if (!SinkObjectWeak.IsValid())
	{
		SinkObjectWeak.Reset();
		SinkInterface = nullptr;
		RefreshSink();
	}

	UObject* SinkObj = SinkObjectWeak.Get();
	if (!SinkObj)
	{
		return false;
	}

	// Ready means a backend is connected AND consent granted on the host side too.
	return ISeam_AnalyticsSink::Execute_IsSinkReady(SinkObj);
}

// ---------------------------------------------------------------------------------------------
// Flushing
// ---------------------------------------------------------------------------------------------

void UAnalytics_Subsystem::FlushInternal()
{
	if (EventBuffer.Num() == 0)
	{
		return;
	}

	// Prefer the resolved seam sink when it is live and ready.
	if (HasLiveSink())
	{
		UObject* SinkObj = SinkObjectWeak.Get();
		if (SinkObj)
		{
			for (const FAnalytics_BufferedEvent& Event : EventBuffer)
			{
				// BlueprintNativeEvent: route through Execute_ so both C++ and BP sinks work.
				ISeam_AnalyticsSink::Execute_RecordAggregateEvent(SinkObj, Event.EventTag, Event.Attributes);
			}
			UE_LOG(LogDP, Verbose, TEXT("Analytics flushed %d event(s) to seam sink."), EventBuffer.Num());
			EventBuffer.Reset();
			return;
		}
	}

	// No live seam sink: use the offline file sink (the safe default) if enabled.
	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	const bool bFileFallback = Settings ? Settings->bEnableFileSinkFallback : true /*defensive fallback*/;
	if (bFileFallback)
	{
		// Hand a plain copy off-thread; never touch the UObject graph from the worker.
		TArray<FAnalytics_BufferedEvent> Snapshot = EventBuffer;
		EventBuffer.Reset();
		FlushToFileSink(Snapshot);
		return;
	}

	// No sink at all and file fallback disabled: keep buffering (capped) rather than spin-dropping
	// silently. The cap enforcement in RecordEvent bounds memory.
	UE_LOG(LogDP, Verbose, TEXT("Analytics flush deferred: no live sink and file fallback disabled (%d buffered)."),
		EventBuffer.Num());
}

void UAnalytics_Subsystem::FlushToFileSink(const TArray<FAnalytics_BufferedEvent>& Snapshot)
{
	if (Snapshot.Num() == 0)
	{
		return;
	}

	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	const FString Subdir = Settings ? Settings->FileSinkSubdirectory : FString(TEXT("Analytics"));

	// Resolve the directory on the game thread (FPaths is game-thread-friendly here), then do the
	// actual disk write on a background task using only the plain copied snapshot + a string path.
	const FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), Subdir);
	const FString FileName = FString::Printf(TEXT("analytics_%s.jsonl"),
		*FDateTime::UtcNow().ToString(TEXT("%Y%m%d")));
	const FString FullPath = FPaths::Combine(Dir, FileName);

	// Pre-serialize on the game thread? No — keep the game thread cheap and serialize off-thread.
	Async(EAsyncExecution::ThreadPool, [Snapshot, Dir, FullPath]()
	{
		IFileManager& FM = IFileManager::Get();
		if (!FM.DirectoryExists(*Dir))
		{
			FM.MakeDirectory(*Dir, /*Tree*/ true);
		}

		FString Lines;
		Lines.Reserve(Snapshot.Num() * 128);
		for (const FAnalytics_BufferedEvent& Event : Snapshot)
		{
			Lines += UAnalytics_Subsystem::EventToJsonLine(Event);
			Lines += LINE_TERMINATOR;
		}

		// Append so multiple flushes in a day accumulate in one file.
		const bool bOk = FFileHelper::SaveStringToFile(
			Lines, *FullPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM,
			&IFileManager::Get(), EFileWrite::FILEWRITE_Append);

		if (!bOk)
		{
			UE_LOG(LogDP, Warning, TEXT("Analytics file sink failed to write %s"), *FullPath);
		}
	});
}

FString UAnalytics_Subsystem::EventToJsonLine(const FAnalytics_BufferedEvent& Event)
{
	// Minimal hand-rolled JSON: one object per line. Keys/values are PII-safe by construction.
	FString Out = FString::Printf(TEXT("{\"event\":\"%s\",\"t\":%f,\"attrs\":{"),
		*Event.EventTag.ToString(), Event.TimestampSeconds);

	bool bFirst = true;
	for (const FSeam_AnalyticsAttr& Attr : Event.Attributes)
	{
		if (!bFirst)
		{
			Out += TEXT(",");
		}
		bFirst = false;
		Out += FString::Printf(TEXT("\"%s\":%s"), *Attr.Key.ToString(), *NetValueToJson(Attr.Value));
	}

	Out += TEXT("}}");
	return Out;
}

bool UAnalytics_Subsystem::TickFlush(float /*DeltaTime*/)
{
	FlushInternal();
	return true; // keep ticking
}

// ---------------------------------------------------------------------------------------------
// Bus bridge
// ---------------------------------------------------------------------------------------------

void UAnalytics_Subsystem::SubscribeToBus()
{
	if (!MessageBus)
	{
		UE_LOG(LogDP, Verbose, TEXT("Analytics: no message bus; bus bridge inactive."));
		return;
	}

	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	const FGameplayTag Channel = Settings ? Settings->BusChannelToObserve : FGameplayTag();
	if (!Channel.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("Analytics: no observed bus channel configured; bus bridge inactive."));
		return;
	}

	// Listen on the subtree so any child channel (DP.Bus.Combat.Death, ...) is observed.
	TWeakObjectPtr<UAnalytics_Subsystem> WeakThis(this);
	BusListenerHandle = MessageBus->ListenNative(
		Channel,
		[WeakThis](const FDP_Message& Message)
		{
			if (UAnalytics_Subsystem* Self = WeakThis.Get())
			{
				Self->HandleBusMessage(Message);
			}
		},
		this,
		EDP_MessageMatch::ExactOrChild);

	UE_LOG(LogDP, Log, TEXT("Analytics observing bus channel '%s'."), *Channel.ToString());
}

void UAnalytics_Subsystem::HandleBusMessage(const FDP_Message& Message)
{
	// Consent gate applies here too: drop observed messages entirely when consent is off.
	if (!bConsentGranted)
	{
		return;
	}

	EnsureEventMapResolved();

	const FGameplayTag SourceChannel = Message.Channel;

	if (EventMap)
	{
		if (const FAnalytics_EventMapEntry* Entry = EventMap->ResolveEntryForChannel(SourceChannel))
		{
			TArray<FSeam_AnalyticsAttr> Attrs;
			EventMap->BuildAttributes(*Entry, SourceChannel, Attrs);
			RecordEvent(Entry->AnalyticsEvent, Attrs);
			return;
		}
	}

	// No explicit mapping: optionally record the catch-all event.
	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	const bool bRecordUnmapped = Settings ? Settings->bRecordUnmappedBusEvents : false;
	if (bRecordUnmapped && SourceChannel.IsValid())
	{
		TArray<FSeam_AnalyticsAttr> Attrs;
		Attrs.Emplace(FName(TEXT("channel")), FSeam_NetValue::MakeTag(SourceChannel));
		RecordEvent(AnalyticsNativeTags::Analytics_Event_BusUnmapped, Attrs);
	}
}

void UAnalytics_Subsystem::EnsureEventMapResolved()
{
	if (bEventMapResolved)
	{
		return;
	}
	bEventMapResolved = true;

	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	if (!Settings || Settings->DefaultEventMap.IsNull())
	{
		return;
	}

	// Synchronous load: the event map is small and consulted per bus message; load once.
	EventMap = Settings->DefaultEventMap.LoadSynchronous();
	if (EventMap)
	{
		UE_LOG(LogDP, Log, TEXT("Analytics loaded event map '%s' with %d entr(ies)."),
			*EventMap->GetName(), EventMap->Entries.Num());
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("Analytics failed to load configured event map asset."));
	}
}

// ---------------------------------------------------------------------------------------------
// Experiments
// ---------------------------------------------------------------------------------------------

FString UAnalytics_Subsystem::ResolvePlayerId() const
{
	const UAnalytics_DeveloperSettings* Settings = UAnalytics_DeveloperSettings::Get();
	if (!Settings || !Settings->PlayerIdProviderServiceTag.IsValid())
	{
		return FString();
	}

	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return FString();
	}

	UObject* Provider = Locator->ResolveService(Settings->PlayerIdProviderServiceTag);
	if (!Provider || !Provider->GetClass()->ImplementsInterface(UAnalytics_PlayerIdProvider::StaticClass()))
	{
		return FString();
	}

	return IAnalytics_PlayerIdProvider::Execute_GetStablePlayerId(Provider);
}

int32 UAnalytics_Subsystem::GetExperimentBucket(FGameplayTag ExperimentTag, int32 NumBuckets) const
{
	if (NumBuckets <= 1)
	{
		return 0;
	}

	// Build a stable seed string. The raw player id is used ONLY to seed the hash here and is
	// never copied into any recorded attribute. When no id is available, fall back to the
	// per-session salt so bucketing is at least stable for the session.
	const FString PlayerId = ResolvePlayerId();

	uint32 Hash;
	if (PlayerId.IsEmpty())
	{
		Hash = HashCombine(SessionSalt, GetTypeHash(ExperimentTag));
	}
	else
	{
		// Hash the (id + experiment) pair; the id is consumed only as hash input.
		Hash = HashCombine(GetTypeHash(PlayerId), GetTypeHash(ExperimentTag));
	}

	return static_cast<int32>(Hash % static_cast<uint32>(NumBuckets));
}

// ---------------------------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------------------------

FString UAnalytics_Subsystem::GetDPDebugString_Implementation() const
{
	const TCHAR* SinkState = SinkObjectWeak.IsValid() ? TEXT("seam") : TEXT("file/none");
	return FString::Printf(
		TEXT("Analytics: consent=%s buffered=%d sink=%s bus=%s"),
		bConsentGranted ? TEXT("on") : TEXT("OFF"),
		EventBuffer.Num(),
		SinkState,
		BusListenerHandle.IsValid() ? TEXT("subscribed") : TEXT("inactive"));
}
