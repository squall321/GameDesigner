// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Funnel/Analytics_FunnelSubsystem.h"

#include "Subsystem/Analytics_Subsystem.h"
#include "Settings/Analytics_TelemetrySettings.h"
#include "Data/Analytics_TelemetryDataAsset.h"
#include "Tags/Analytics_TelemetryTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Net/Seam_NetValue.h"
#include "Analytics/Seam_AnalyticsSink.h"

#include "Engine/GameInstance.h"
#include "Misc/App.h"

namespace
{
	const FName GAttr_Funnel(TEXT("funnel"));
	const FName GAttr_Step(TEXT("step"));
	const FName GAttr_Depth(TEXT("depth"));
	const FName GAttr_Completed(TEXT("completed"));
	const FName GAttr_DurationSeconds(TEXT("duration_s"));
	const FName GAttr_CohortBucket(TEXT("cohort_bucket"));

	/** Stable attribute-key prefix for a folded cohort dimension (e.g. "cohort.<dimension>"). */
	FName MakeCohortKey(const FGameplayTag& Dimension)
	{
		return FName(*FString::Printf(TEXT("cohort.%s"), *Dimension.ToString()));
	}
}

void UAnalytics_FunnelSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UAnalytics_TelemetrySettings* Settings = UAnalytics_TelemetrySettings::Get();
	const bool bEnabled = Settings ? Settings->bEnableFunnel : true;
	if (!bEnabled)
	{
		UE_LOG(LogDP, Log, TEXT("Analytics FunnelSubsystem disabled by settings."));
		return;
	}

	// Idle-timeout sweep ticker (only when a positive timeout is configured).
	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const float Timeout = Data ? Data->GetEffectiveFunnelTimeoutSeconds() : 0.f;
	if (Timeout > 0.f)
	{
		// Sweep cadence: sample at a fraction of the timeout so we catch expiries promptly but
		// cheaply. Clamp to a sane minimum so a tiny timeout doesn't tick every frame.
		const float SweepInterval = FMath::Max(0.5f, Timeout * 0.25f);
		TimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UAnalytics_FunnelSubsystem::TickTimeoutSweep), SweepInterval);
	}

	UE_LOG(LogDP, Log, TEXT("Analytics FunnelSubsystem initialized (timeout=%.1fs)."), Timeout);
}

void UAnalytics_FunnelSubsystem::Deinitialize()
{
	// Summarise any still-open runs so a clean shutdown does not drop in-flight funnels.
	for (TPair<FGameplayTag, FAnalytics_FunnelRun>& Pair : Runs)
	{
		SummariseRun(Pair.Key, Pair.Value, /*bCompleted*/ false);
	}
	Runs.Reset();
	CohortTags.Reset();

	if (TimeoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TimeoutTickerHandle);
		TimeoutTickerHandle.Reset();
	}

	CachedAnalyticsSubsystem.Reset();
	CachedDataAsset.Reset();

	Super::Deinitialize();
}

UAnalytics_Subsystem* UAnalytics_FunnelSubsystem::ResolveAnalyticsSubsystem()
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

const UAnalytics_TelemetryDataAsset* UAnalytics_FunnelSubsystem::ResolveDataAsset()
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

void UAnalytics_FunnelSubsystem::BeginFunnel(FGameplayTag FunnelId)
{
	if (!FunnelId.IsValid())
	{
		return;
	}

	// Restarting an in-flight run summarises the previous one as abandoned first.
	if (FAnalytics_FunnelRun* Existing = Runs.Find(FunnelId))
	{
		SummariseRun(FunnelId, *Existing, /*bCompleted*/ false);
	}

	FAnalytics_FunnelRun& Run = Runs.FindOrAdd(FunnelId);
	Run = FAnalytics_FunnelRun();
	Run.BeginTimeSeconds = FApp::GetCurrentTime();
	Run.LastAdvanceTimeSeconds = Run.BeginTimeSeconds;

	if (UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem())
	{
		TArray<FSeam_AnalyticsAttr> Attrs;
		Attrs.Emplace(GAttr_Funnel, FSeam_NetValue::MakeTag(FunnelId));
		Analytics->RecordEvent(AnalyticsTelemetryTags::Event_Funnel_Begin, Attrs);
	}
}

bool UAnalytics_FunnelSubsystem::AdvanceFunnelStep(FGameplayTag FunnelId, FGameplayTag StepTag)
{
	if (!FunnelId.IsValid() || !StepTag.IsValid())
	{
		return false;
	}

	FAnalytics_FunnelRun* Run = Runs.Find(FunnelId);
	if (!Run)
	{
		// Implicit begin so callers can fire steps directly.
		BeginFunnel(FunnelId);
		Run = Runs.Find(FunnelId);
		if (!Run)
		{
			return false;
		}
	}

	if (Run->StepsReached.Contains(StepTag))
	{
		return false; // idempotent per step
	}

	Run->StepsReached.Add(StepTag);
	Run->LastAdvanceTimeSeconds = FApp::GetCurrentTime();

	if (UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem())
	{
		TArray<FSeam_AnalyticsAttr> Attrs;
		Attrs.Emplace(GAttr_Funnel, FSeam_NetValue::MakeTag(FunnelId));
		Attrs.Emplace(GAttr_Step, FSeam_NetValue::MakeTag(StepTag));
		Attrs.Emplace(GAttr_Depth, FSeam_NetValue::MakeInt(Run->Depth()));
		Analytics->RecordEvent(AnalyticsTelemetryTags::Event_Funnel_Step, Attrs);
	}
	return true;
}

void UAnalytics_FunnelSubsystem::CompleteFunnel(FGameplayTag FunnelId)
{
	if (FAnalytics_FunnelRun* Run = Runs.Find(FunnelId))
	{
		SummariseRun(FunnelId, *Run, /*bCompleted*/ true);
		Runs.Remove(FunnelId);
	}
}

void UAnalytics_FunnelSubsystem::SetCohortTag(FGameplayTag CohortDimension, FGameplayTag CohortValue)
{
	if (!CohortDimension.IsValid())
	{
		return;
	}
	if (CohortValue.IsValid())
	{
		CohortTags.Add(CohortDimension, CohortValue);
	}
	else
	{
		CohortTags.Remove(CohortDimension);
	}
}

FGameplayTag UAnalytics_FunnelSubsystem::GetCohortTag(FGameplayTag CohortDimension) const
{
	const FGameplayTag* Found = CohortTags.Find(CohortDimension);
	return Found ? *Found : FGameplayTag();
}

int32 UAnalytics_FunnelSubsystem::GetFunnelDepth(FGameplayTag FunnelId) const
{
	const FAnalytics_FunnelRun* Run = Runs.Find(FunnelId);
	return Run ? Run->Depth() : 0;
}

void UAnalytics_FunnelSubsystem::SummariseRun(FGameplayTag FunnelId, FAnalytics_FunnelRun& Run, bool bCompleted)
{
	if (Run.bSummarised)
	{
		return;
	}
	Run.bSummarised = true;

	UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem();
	if (!Analytics)
	{
		return;
	}

	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const int32 CohortBuckets = Data ? Data->GetEffectiveFunnelCohortBucketCount() : 16;

	TArray<FSeam_AnalyticsAttr> Attrs;
	Attrs.Emplace(GAttr_Funnel, FSeam_NetValue::MakeTag(FunnelId));
	Attrs.Emplace(GAttr_Depth, FSeam_NetValue::MakeInt(Run.Depth()));
	Attrs.Emplace(GAttr_Completed, FSeam_NetValue::MakeBool(bCompleted));
	Attrs.Emplace(GAttr_DurationSeconds,
		FSeam_NetValue::MakeFloat(FApp::GetCurrentTime() - Run.BeginTimeSeconds));

	// Coarse, non-reversible player cohort from the CORE subsystem (privacy guard). The funnel id is
	// used as the experiment-tag input so the same player buckets stably for this funnel.
	const int32 Bucket = Analytics->GetExperimentBucket(FunnelId, CohortBuckets);
	Attrs.Emplace(GAttr_CohortBucket, FSeam_NetValue::MakeInt(Bucket));

	// Fold every active session cohort dimension as a Tag attribute (privacy-safe).
	for (const TPair<FGameplayTag, FGameplayTag>& Cohort : CohortTags)
	{
		Attrs.Emplace(MakeCohortKey(Cohort.Key), FSeam_NetValue::MakeTag(Cohort.Value));
	}

	Analytics->RecordEvent(AnalyticsTelemetryTags::Event_Funnel_Summary, Attrs);
}

bool UAnalytics_FunnelSubsystem::TickTimeoutSweep(float /*DeltaTime*/)
{
	const UAnalytics_TelemetryDataAsset* Data = ResolveDataAsset();
	const float Timeout = Data ? Data->GetEffectiveFunnelTimeoutSeconds() : 0.f;
	if (Timeout <= 0.f)
	{
		return true; // keep ticking; timeout may be reconfigured via a hotfix
	}

	const double Now = FApp::GetCurrentTime();
	TArray<FGameplayTag> Expired;
	for (TPair<FGameplayTag, FAnalytics_FunnelRun>& Pair : Runs)
	{
		if ((Now - Pair.Value.LastAdvanceTimeSeconds) >= Timeout)
		{
			SummariseRun(Pair.Key, Pair.Value, /*bCompleted*/ false);
			Expired.Add(Pair.Key);
		}
	}
	for (const FGameplayTag& Tag : Expired)
	{
		Runs.Remove(Tag);
	}
	return true; // keep ticking
}

FString UAnalytics_FunnelSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Funnel: runs=%d cohorts=%d"), Runs.Num(), CohortTags.Num());
}
