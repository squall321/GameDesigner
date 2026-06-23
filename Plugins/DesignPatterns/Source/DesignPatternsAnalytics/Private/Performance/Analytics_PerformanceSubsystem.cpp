// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Performance/Analytics_PerformanceSubsystem.h"

#include "Subsystem/Analytics_Subsystem.h"
#include "Settings/Analytics_TelemetrySettings.h"
#include "Data/Analytics_TelemetryDataAsset.h"
#include "Tags/Analytics_TelemetryTags.h"

#include "Core/DPLog.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Net/Seam_NetValue.h"

#include "Engine/GameInstance.h"
#include "Misc/App.h"
#include "HAL/PlatformMemory.h"

namespace
{
	const FName GAttr_Reason(TEXT("reason"));
	const FName GAttr_Hitches(TEXT("hitches"));
	const FName GAttr_UsedPhysMB(TEXT("used_phys_mb"));

	/** Attribute key for a percentile value, e.g. "frame_ms_p90". */
	FName MakePercentileKey(float Percentile01)
	{
		const int32 Pct = FMath::RoundToInt(Percentile01 * 100.f);
		return FName(*FString::Printf(TEXT("frame_ms_p%d"), Pct));
	}
}

void UAnalytics_PerformanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	if (Settings && !Settings->bEnablePerformance)
	{
		UE_LOG(LogDP, Log, TEXT("Analytics PerformanceSubsystem disabled by settings."));
		return;
	}

	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const int32 RingSize = Data ? Data->GetEffectivePerfRingSize() : 512;
	FrameMsRing.Reset();
	FrameMsRing.Reserve(RingSize);

	const float Interval = Data ? Data->GetEffectivePerfSampleInterval() : 1.f;
	if (Interval > 0.f)
	{
		SampleTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UAnalytics_PerformanceSubsystem::TickSample), Interval);
	}

	UE_LOG(LogDP, Log, TEXT("Analytics PerformanceSubsystem initialized (sample=%.2fs ring=%d)."),
		Interval, RingSize);
}

void UAnalytics_PerformanceSubsystem::Deinitialize()
{
	// Final summary so a clean shutdown does not drop the running window. We reuse the Perf.Sample
	// event id (the reason attribute is omitted) rather than couple to the core area's session tags.
	if (FrameMsRing.Num() > 0)
	{
		EmitPerfSummary(FGameplayTag());
	}

	if (SampleTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(SampleTickerHandle);
		SampleTickerHandle.Reset();
	}

	FrameMsRing.Reset();
	CachedAnalyticsSubsystem.Reset();
	CachedDataAsset.Reset();

	Super::Deinitialize();
}

UAnalytics_Subsystem* UAnalytics_PerformanceSubsystem::ResolveAnalyticsSubsystem()
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

const UAnalytics_TelemetryDataAsset* UAnalytics_PerformanceSubsystem::ResolveDataAsset()
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

void UAnalytics_PerformanceSubsystem::PushFrameSample(float FrameMs, int32 RingSize)
{
	if (RingSize < 1)
	{
		RingSize = 1;
	}

	if (FrameMsRing.Num() < RingSize)
	{
		FrameMsRing.Add(FrameMs);
		if (FrameMsRing.Num() >= RingSize)
		{
			bRingFull = true;
			RingCursor = 0;
		}
	}
	else
	{
		// Ring is full (or the configured size shrank): overwrite in a wrapping cursor.
		if (RingCursor >= FrameMsRing.Num())
		{
			RingCursor = 0;
		}
		FrameMsRing[RingCursor] = FrameMs;
		RingCursor = (RingCursor + 1) % FrameMsRing.Num();
		bRingFull = true;
	}
}

void UAnalytics_PerformanceSubsystem::SampleNow()
{
	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const int32 RingSize = Data ? Data->GetEffectivePerfRingSize() : 512;
	const float HitchMs = Data ? Data->GetEffectiveHitchThresholdMs() : 100.f;

	const float FrameMs = static_cast<float>(FApp::GetDeltaTime()) * 1000.f;
	PushFrameSample(FrameMs, RingSize);

	// Memory snapshot (engine stats only).
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	LastUsedPhysicalMB = static_cast<double>(MemStats.UsedPhysical) / (1024.0 * 1024.0);

	// Hitch detection.
	if (FrameMs >= HitchMs)
	{
		++HitchCountSinceEmit;
		if (UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem())
		{
			TArray<FSeam_AnalyticsAttr> Attrs;
			Attrs.Emplace(MakePercentileKey(1.f), FSeam_NetValue::MakeFloat(FrameMs)); // the hitch frame ms
			Analytics->RecordEvent(AnalyticsTelemetryTags::Event_Perf_Hitch, Attrs);
		}
	}
}

float UAnalytics_PerformanceSubsystem::PercentileOfSorted(const TArray<float>& Sorted, float Percentile01)
{
	if (Sorted.Num() == 0)
	{
		return 0.f;
	}
	const float Clamped = FMath::Clamp(Percentile01, 0.f, 1.f);
	// Nearest-rank: index = ceil(p * N) - 1, clamped into range.
	int32 Index = FMath::CeilToInt(Clamped * static_cast<float>(Sorted.Num())) - 1;
	Index = FMath::Clamp(Index, 0, Sorted.Num() - 1);
	return Sorted[Index];
}

float UAnalytics_PerformanceSubsystem::GetPercentileFrameMs(float Percentile01) const
{
	if (FrameMsRing.Num() == 0)
	{
		return 0.f;
	}
	TArray<float> Sorted = FrameMsRing;
	Sorted.Sort();
	return PercentileOfSorted(Sorted, Percentile01);
}

void UAnalytics_PerformanceSubsystem::EmitPerfSummary(FGameplayTag Reason)
{
	UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem();
	if (!Analytics)
	{
		HitchCountSinceEmit = 0;
		return;
	}

	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const TArray<float> Percentiles = Data ? Data->GetEffectivePerfPercentiles() : TArray<float>{ 0.5f, 0.9f, 0.99f };

	TArray<float> Sorted = FrameMsRing;
	Sorted.Sort();

	TArray<FSeam_AnalyticsAttr> Attrs;
	if (Reason.IsValid())
	{
		Attrs.Emplace(GAttr_Reason, FSeam_NetValue::MakeTag(Reason));
	}
	for (const float P : Percentiles)
	{
		Attrs.Emplace(MakePercentileKey(P), FSeam_NetValue::MakeFloat(PercentileOfSorted(Sorted, P)));
	}
	Attrs.Emplace(GAttr_Hitches, FSeam_NetValue::MakeInt(HitchCountSinceEmit));
	Attrs.Emplace(GAttr_UsedPhysMB, FSeam_NetValue::MakeFloat(LastUsedPhysicalMB));

	Analytics->RecordEvent(AnalyticsTelemetryTags::Event_Perf_Sample, Attrs);

	HitchCountSinceEmit = 0;
}

bool UAnalytics_PerformanceSubsystem::TickSample(float DeltaTime)
{
	SampleNow();

	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const float EmitInterval = Data ? Data->GetEffectivePerfEmitInterval() : 30.f;
	if (EmitInterval > 0.f)
	{
		TimeSinceEmit += DeltaTime;
		if (TimeSinceEmit >= EmitInterval)
		{
			TimeSinceEmit = 0.f;
			EmitPerfSummary(AnalyticsTelemetryTags::Event_Perf_Sample);
		}
	}
	return true; // keep ticking
}

FString UAnalytics_PerformanceSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Perf: samples=%d p50=%.1fms p99=%.1fms hitches=%d memMB=%.0f"),
		FrameMsRing.Num(),
		GetPercentileFrameMs(0.5f),
		GetPercentileFrameMs(0.99f),
		HitchCountSinceEmit,
		LastUsedPhysicalMB);
}
