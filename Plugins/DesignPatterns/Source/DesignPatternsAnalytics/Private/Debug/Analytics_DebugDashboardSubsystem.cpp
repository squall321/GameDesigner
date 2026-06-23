// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Debug/Analytics_DebugDashboardSubsystem.h"

#include "Subsystem/Analytics_Subsystem.h"
#include "Settings/Analytics_TelemetrySettings.h"
#include "Data/Analytics_TelemetryDataAsset.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Net/Seam_NetValue.h"

#include "Engine/GameInstance.h"
#include "Misc/App.h"

void UAnalytics_DebugDashboardSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	if (Settings && Settings->bEnableDebugDashboard)
	{
		BeginAggregation();
	}
	else
	{
		UE_LOG(LogDP, Log, TEXT("Analytics DebugDashboardSubsystem inert (disabled by settings)."));
	}
}

void UAnalytics_DebugDashboardSubsystem::Deinitialize()
{
	EndAggregation();
	RollingCounts.Reset();
	Recent.Reset();
	CachedAnalyticsSubsystem.Reset();
	CachedDataAsset.Reset();
	Super::Deinitialize();
}

UAnalytics_Subsystem* UAnalytics_DebugDashboardSubsystem::ResolveAnalyticsSubsystem()
{
	if (UAnalytics_Subsystem* Cached = CachedAnalyticsSubsystem.Get())
	{
		return Cached;
	}
	if (UGameInstance* GI = GetGameInstance())
	{
		UAnalytics_Subsystem* Sub = GI->GetSubsystem<UAnalytics_Subsystem>();
		CachedAnalyticsSubsystem = Sub;
		return Sub;
	}
	return nullptr;
}

const UAnalytics_TelemetryDataAsset* UAnalytics_DebugDashboardSubsystem::ResolveDataAsset()
{
	if (const UAnalytics_TelemetryDataAsset* Cached = CachedDataAsset.Get())
	{
		return Cached;
	}
	if (bDataAssetResolutionAttempted)
	{
		return nullptr;
	}
	bDataAssetResolutionAttempted = true;

	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	if (!Settings || !Settings->TelemetryDataTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		const UAnalytics_TelemetryDataAsset* Asset = Registry->Find<UAnalytics_TelemetryDataAsset>(Settings->TelemetryDataTag);
		CachedDataAsset = Asset;
		return Asset;
	}
	return nullptr;
}

void UAnalytics_DebugDashboardSubsystem::BeginAggregation()
{
	if (bActive)
	{
		return;
	}

	UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem();
	if (!Analytics)
	{
		UE_LOG(LogDP, Verbose, TEXT("DebugDashboard: no core analytics subsystem; aggregation deferred."));
		return;
	}

	// Bind to the additive in-process observer hook (post-consent, game thread).
	EventRecordedHandle = Analytics->OnEventRecorded.AddUObject(
		this, &UAnalytics_DebugDashboardSubsystem::HandleEventRecorded);

	// Throttle ticker cadence from the data asset (>= a tiny floor so a 0 throttle still ticks cheaply).
	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const float Throttle = Data ? Data->GetEffectiveDashboardThrottleSeconds() : 0.25f;
	const float TickInterval = FMath::Max(0.05f, Throttle);
	ThrottleTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAnalytics_DebugDashboardSubsystem::TickThrottle), TickInterval);

	bActive = true;
	UE_LOG(LogDP, Log, TEXT("Analytics DebugDashboardSubsystem aggregating (throttle=%.2fs)."), Throttle);
}

void UAnalytics_DebugDashboardSubsystem::EndAggregation()
{
	if (EventRecordedHandle.IsValid())
	{
		if (UAnalytics_Subsystem* Analytics = CachedAnalyticsSubsystem.Get())
		{
			Analytics->OnEventRecorded.Remove(EventRecordedHandle);
		}
		EventRecordedHandle.Reset();
	}
	if (ThrottleTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(ThrottleTickerHandle);
		ThrottleTickerHandle.Reset();
	}
	bActive = false;
}

void UAnalytics_DebugDashboardSubsystem::SetEnabled(bool bEnabled)
{
	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	if (Settings && !Settings->bEnableDebugDashboard)
	{
		// Master switch wins; SetEnabled cannot force-on a shipping-disabled dashboard.
		UE_LOG(LogDP, Verbose, TEXT("DebugDashboard SetEnabled ignored: master switch off."));
		return;
	}

	if (bEnabled)
	{
		BeginAggregation();
	}
	else
	{
		EndAggregation();
	}
}

void UAnalytics_DebugDashboardSubsystem::HandleEventRecorded(FGameplayTag EventTag, const TArray<FSeam_AnalyticsAttr>& Attributes)
{
	if (!EventTag.IsValid())
	{
		return;
	}

	RollingCounts.FindOrAdd(EventTag) += 1;
	++TotalObserved;

	FAnalytics_Breadcrumb& View = Recent.AddDefaulted_GetRef();
	View.Tag = EventTag;
	View.TimestampSeconds = FApp::GetCurrentTime();
	View.Attrs = Attributes; // PII-safe FSeam_NetValue values

	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const int32 RecentCap = Data ? Data->GetEffectiveDashboardRecentEventCount() : 32;
	if (Recent.Num() > RecentCap)
	{
		Recent.RemoveAt(0, Recent.Num() - RecentCap, /*bAllowShrinking*/ false);
	}

	bDirty = true;
}

FAnalytics_DashboardSnapshot UAnalytics_DebugDashboardSubsystem::BuildSnapshot() const
{
	FAnalytics_DashboardSnapshot Snapshot;
	Snapshot.RollingCounts = RollingCounts;
	Snapshot.Recent = Recent;
	Snapshot.TotalObserved = TotalObserved;
	return Snapshot;
}

FAnalytics_DashboardSnapshot UAnalytics_DebugDashboardSubsystem::GetSnapshot() const
{
	return BuildSnapshot();
}

int32 UAnalytics_DebugDashboardSubsystem::GetEventCount(FGameplayTag EventTag) const
{
	const int32* Found = RollingCounts.Find(EventTag);
	return Found ? *Found : 0;
}

bool UAnalytics_DebugDashboardSubsystem::TickThrottle(float /*DeltaTime*/)
{
	if (bDirty)
	{
		bDirty = false;
		OnDashboardUpdated.Broadcast(BuildSnapshot());
	}
	return true; // keep ticking
}

FString UAnalytics_DebugDashboardSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Dashboard: active=%s tags=%d total=%d recent=%d"),
		bActive ? TEXT("yes") : TEXT("no"), RollingCounts.Num(), TotalObserved, Recent.Num());
}
