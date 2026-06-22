// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Events/WorldHub_EventLogSubsystem.h"
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "History/WorldHub_HistorySubsystem.h"
#include "WorldHub_NativeTags.h"

#include "Clock/Seam_SimClock.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

void UWorldHub_EventLogSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ResolveHub();
	RegisterSelfAsService(/*bRegister=*/true);

	UE_LOG(LogDP, Log, TEXT("[WorldHub] Event log initialized (authority=%s, maxEvents=%d)."),
		HasWorldAuthority() ? TEXT("yes") : TEXT("no"), MaxEvents);
}

void UWorldHub_EventLogSubsystem::Deinitialize()
{
	if (UWorldHub_StateHubSubsystem* H = Hub.Get())
	{
		H->OnValueChanged.RemoveAll(this);
	}
	RegisterSelfAsService(/*bRegister=*/false);

	Events.Reset();
	Hub.Reset();
	AnalyticsSink.Reset();
	CachedAnalyticsObject.Reset();

	Super::Deinitialize();
}

bool UWorldHub_EventLogSubsystem::HasWorldAuthority() const
{
	const UWorld* W = GetWorld();
	return W && W->GetNetMode() != NM_Client;
}

//~ Resolution helpers -------------------------------------------------------------------------

UWorldHub_StateHubSubsystem* UWorldHub_EventLogSubsystem::ResolveHub()
{
	if (UWorldHub_StateHubSubsystem* Cached = Hub.Get())
	{
		return Cached;
	}
	UWorldHub_StateHubSubsystem* Resolved =
		FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
	if (Resolved)
	{
		Hub = Resolved;
		Resolved->OnValueChanged.AddUniqueDynamic(this, &UWorldHub_EventLogSubsystem::OnHubValueChanged);
	}
	return Resolved;
}

ISeam_AnalyticsSink* UWorldHub_EventLogSubsystem::ResolveAnalyticsSink()
{
	if (UObject* Cached = CachedAnalyticsObject.Get())
	{
		if (ISeam_AnalyticsSink* AsSink = Cast<ISeam_AnalyticsSink>(Cached))
		{
			return AsSink;
		}
	}
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		static const FGameplayTag AnalyticsKey =
			FGameplayTag::RequestGameplayTag(FName("DP.Service.Analytics"), /*ErrorIfNotFound=*/false);
		if (AnalyticsKey.IsValid())
		{
			if (UObject* Provider = Locator->ResolveService(AnalyticsKey))
			{
				if (ISeam_AnalyticsSink* AsSink = Cast<ISeam_AnalyticsSink>(Provider))
				{
					CachedAnalyticsObject = Provider;
					AnalyticsSink = AsSink;
					return AsSink;
				}
			}
		}
	}
	return nullptr;
}

//~ Append -------------------------------------------------------------------------------------

void UWorldHub_EventLogSubsystem::OnHubValueChanged(FWorldHub_Scope Scope, FGameplayTag Key, FSeam_NetValue NewValue)
{
	// AUTHORITY GUARD at the TOP: only the server appends canonical events. Skip the echo we produce
	// while replaying into a hub (it re-enters OnValueChanged on the same authority).
	if (!HasWorldAuthority() || bReplaying)
	{
		return;
	}

	FWorldHub_HubEvent Event;
	Event.Scope = Scope;
	Event.Key = Key;
	Event.NetValue = NewValue;
	Event.ChangeKind = NewValue.IsSet() ? EWorldHub_ChangeKind::Set : EWorldHub_ChangeKind::Clear;

	// Capture the lossless local payload (LOCAL/SAVE only) from the hub if it has one.
	if (UWorldHub_StateHubSubsystem* H = Hub.Get())
	{
		FInstancedStruct Local;
		if (H->GetVariable(Key, Scope, Local) && Local.IsValid())
		{
			Event.LocalValue = Local;
		}
	}

	AppendEvent(Event);
}

void UWorldHub_EventLogSubsystem::AppendEvent(const FWorldHub_HubEvent& InEvent)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasWorldAuthority())
	{
		return;
	}

	FWorldHub_HubEvent Event = InEvent;
	Event.Sequence = NextSequence++;

	// Deterministic, pause-aware timestamps from the shared clock (optional).
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		static const FGameplayTag ClockKey =
			FGameplayTag::RequestGameplayTag(FName("DP.Service.SimClock"), /*ErrorIfNotFound=*/false);
		if (ClockKey.IsValid())
		{
			if (UObject* Provider = Locator->ResolveService(ClockKey))
			{
				if (ISeam_SimClock* AsClock = Cast<ISeam_SimClock>(Provider))
				{
					Event.DayNumber = ISeam_SimClock::Execute_GetDayNumber(Provider);
					// SimTime: prefer a normalized-day composite so the value is monotone-ish for replay.
					Event.SimTime = static_cast<double>(Event.DayNumber)
						+ ISeam_SimClock::Execute_GetNormalizedTimeOfDay(Provider);
				}
			}
		}
	}

	Events.Add(Event);

	// Trim oldest beyond the finite cap (0 = unbounded opt-in).
	while (MaxEvents > 0 && Events.Num() > MaxEvents)
	{
		Events.RemoveAt(0, 1, /*bAllowShrinking=*/false);
	}

	if (bMirrorToAnalytics)
	{
		MirrorEventToAnalytics(Event);
	}

	const FWorldHub_FlatEvent Flat(Event);
	OnEventAppended.Broadcast(Flat);
}

void UWorldHub_EventLogSubsystem::MirrorEventToAnalytics(const FWorldHub_HubEvent& Event)
{
	if (!AnalyticsEventTag.IsValid())
	{
		return;
	}
	ISeam_AnalyticsSink* Sink = ResolveAnalyticsSink();
	if (!Sink || !ISeam_AnalyticsSink::Execute_IsSinkReady(CachedAnalyticsObject.Get()))
	{
		return;
	}

	// PII-safe: only FSeam_NetValue-typed attributes (no FText, no free-form ids).
	TArray<FSeam_AnalyticsAttr> Attrs;
	Attrs.Emplace(FName("Key"), FSeam_NetValue::MakeTag(Event.Key));
	Attrs.Emplace(FName("ChangeKind"), FSeam_NetValue::MakeInt(static_cast<int64>(Event.ChangeKind)));
	Attrs.Emplace(FName("ScopeType"), FSeam_NetValue::MakeInt(static_cast<int64>(Event.Scope.ScopeType)));
	Attrs.Emplace(FName("Sequence"), FSeam_NetValue::MakeInt(Event.Sequence));
	if (Event.NetValue.IsSet())
	{
		Attrs.Emplace(FName("Value"), Event.NetValue);
	}

	ISeam_AnalyticsSink::Execute_RecordAggregateEvent(CachedAnalyticsObject.Get(), AnalyticsEventTag, Attrs);
}

//~ Query --------------------------------------------------------------------------------------

void UWorldHub_EventLogSubsystem::QueryEvents(FGameplayTag KeyFilter, FWorldHub_Scope ScopeFilter, int64 SinceSequence, TArray<FWorldHub_HubEvent>& Out) const
{
	Out.Reset();

	const bool bFilterKey = KeyFilter.IsValid();
	// A Global scope filter is treated as "match all scopes"; a Faction/Entity filter matches exactly.
	const bool bFilterScope = ScopeFilter.ScopeType != EWorldHub_ScopeType::Global;

	for (const FWorldHub_HubEvent& Event : Events)
	{
		if (Event.Sequence <= SinceSequence)
		{
			continue;
		}
		if (bFilterKey && !Event.Key.MatchesTag(KeyFilter))
		{
			continue;
		}
		if (bFilterScope && Event.Scope != ScopeFilter)
		{
			continue;
		}
		Out.Add(Event);
	}
}

//~ Replay -------------------------------------------------------------------------------------

bool UWorldHub_EventLogSubsystem::ReplayInto(UWorldHub_StateHubSubsystem* TargetHub, int64 UpToSequence)
{
	// AUTHORITY GUARD at the TOP: replay mutates authoritative state.
	if (!HasWorldAuthority() || !TargetHub || !TargetHub->HasWorldAuthority())
	{
		return false;
	}

	const int64 Limit = (UpToSequence <= 0) ? TNumericLimits<int64>::Max() : UpToSequence;

	TGuardValue<bool> ReplayGuard(bReplaying, true);
	for (const FWorldHub_HubEvent& Event : Events)
	{
		if (Event.Sequence > Limit)
		{
			break;
		}
		switch (Event.ChangeKind)
		{
		case EWorldHub_ChangeKind::Clear:
			TargetHub->ClearValue(Event.Key, Event.Scope);
			break;

		case EWorldHub_ChangeKind::Set:
		case EWorldHub_ChangeKind::Increment:
		default:
			if (Event.LocalValue.IsValid())
			{
				TargetHub->SetVariable(Event.Key, Event.LocalValue, Event.Scope);
			}
			else if (Event.NetValue.IsSet())
			{
				TargetHub->SetNetValue(Event.Key, Event.NetValue, Event.Scope);
			}
			break;
		}
	}
	return true;
}

//~ ISeam_HubHistory ---------------------------------------------------------------------------

bool UWorldHub_EventLogSubsystem::RewindToCheckpoint(FGameplayTag CheckpointLabel)
{
	// The raw event log has no checkpoint frames; delegate to the history subsystem if it exists.
	if (UWorldHub_HistorySubsystem* History =
		FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_HistorySubsystem>(this))
	{
		return History->RewindToCheckpoint(CheckpointLabel);
	}
	return false;
}

int64 UWorldHub_EventLogSubsystem::GetLatestEventSequence() const
{
	return Events.Num() > 0 ? Events.Last().Sequence : 0;
}

void UWorldHub_EventLogSubsystem::GetEventsSince(int64 Sequence, TArray<FInstancedStruct>& OutFlattened) const
{
	OutFlattened.Reset();
	for (const FWorldHub_HubEvent& Event : Events)
	{
		if (Event.Sequence > Sequence)
		{
			// Flatten to the wire-safe, FInstancedStruct-free boundary form (boxed for the seam).
			FWorldHub_FlatEvent Flat(Event);
			FInstancedStruct& Boxed = OutFlattened.AddDefaulted_GetRef();
			Boxed.InitializeAs<FWorldHub_FlatEvent>(Flat);
		}
	}
}

//~ Service / debug ----------------------------------------------------------------------------

void UWorldHub_EventLogSubsystem::RegisterSelfAsService(bool bRegister)
{
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}
	if (bRegister)
	{
		Locator->RegisterService(WorldHubNativeTags::Service_WorldHub_EventLog, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
	else if (Locator->ResolveService(WorldHubNativeTags::Service_WorldHub_EventLog) == this)
	{
		Locator->UnregisterService(WorldHubNativeTags::Service_WorldHub_EventLog);
	}
}

FString UWorldHub_EventLogSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("WorldHub EventLog [%s] Events=%d/%d Seq=%lld"),
		HasWorldAuthority() ? TEXT("AUTH") : TEXT("client"),
		Events.Num(), MaxEvents, GetLatestEventSequence());
}
