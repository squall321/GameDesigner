// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Progression/Analytics_ProgressionComponent.h"
#include "Progression/Analytics_ProgressionDataAsset.h"
#include "Experiment/Analytics_ExperimentTags.h"
#include "Subsystem/Analytics_Subsystem.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Net/Seam_NetValue.h"
#include "Analytics/Seam_AnalyticsSink.h"

#include "Engine/World.h"

namespace
{
	const FName GAttr_Funnel(TEXT("funnel"));
	const FName GAttr_Step(TEXT("step"));
	const FName GAttr_StepIndex(TEXT("step_index"));
	const FName GAttr_FunnelDepth(TEXT("funnel_depth"));
	const FName GAttr_PlaytimeSeconds(TEXT("playtime_seconds"));
	const FName GAttr_IsMilestone(TEXT("is_milestone"));
}

UAnalytics_ProgressionComponent::UAnalytics_ProgressionComponent()
{
	// We need a tick for playtime accumulation, but it is cheap and only does float math; disable
	// when playtime tracking is off (toggled in BeginPlay based on bTrackPlaytime).
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	// Telemetry is per-machine; the component never replicates.
	SetIsReplicatedByDefault(false);
}

void UAnalytics_ProgressionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!bTrackPlaytime)
	{
		PrimaryComponentTick.SetTickFunctionEnable(false);
	}

	UE_LOG(LogDP, Verbose, TEXT("ProgressionComponent BeginPlay on '%s' (funnel=%s, trackPlaytime=%s)."),
		*GetNameSafe(GetOwner()),
		*FunnelDefinitionTag.GetTagName().ToString(),
		bTrackPlaytime ? TEXT("yes") : TEXT("no"));
}

void UAnalytics_ProgressionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Emit a final heartbeat so the last partial interval of playtime is not lost (only if there is
	// something to report and recording is possible — the subsystem itself is consent-gated).
	if (bTrackPlaytime && AccumulatedPlaytimeSeconds > 0.0f)
	{
		EmitPlaytimeHeartbeat();
	}

	Super::EndPlay(EndPlayReason);
}

void UAnalytics_ProgressionComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bTrackPlaytime)
	{
		return;
	}

	AccumulatedPlaytimeSeconds += DeltaTime;
	TimeSinceHeartbeat += DeltaTime;

	const UAnalytics_ProgressionDataAsset* Funnel = ResolveFunnelAsset();
	// Heartbeat interval is data; <= 0 disables periodic heartbeats. Fall back to no heartbeat when
	// the funnel asset is missing (playtime still accumulates and is reported on EndPlay).
	const float Interval = Funnel ? Funnel->PlaytimeHeartbeatSeconds : 0.0f;
	if (Interval > 0.0f && TimeSinceHeartbeat >= Interval)
	{
		EmitPlaytimeHeartbeat();
	}
}

const UAnalytics_ProgressionDataAsset* UAnalytics_ProgressionComponent::ResolveFunnelAsset()
{
	if (const UAnalytics_ProgressionDataAsset* Cached = CachedFunnelAsset.Get())
	{
		return Cached;
	}
	if (bFunnelResolutionAttempted)
	{
		// Already tried and failed (e.g. unset tag or missing asset); don't thrash the registry.
		return nullptr;
	}
	bFunnelResolutionAttempted = true;

	if (!FunnelDefinitionTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		const UAnalytics_ProgressionDataAsset* Asset = Registry->Find<UAnalytics_ProgressionDataAsset>(FunnelDefinitionTag);
		CachedFunnelAsset = Asset;
		return Asset;
	}
	return nullptr;
}

UAnalytics_Subsystem* UAnalytics_ProgressionComponent::ResolveAnalyticsSubsystem()
{
	if (UAnalytics_Subsystem* Cached = CachedAnalyticsSubsystem.Get())
	{
		return Cached;
	}
	UAnalytics_Subsystem* Sub = FDP_SubsystemStatics::GetGameInstanceSubsystem<UAnalytics_Subsystem>(this);
	CachedAnalyticsSubsystem = Sub;
	return Sub;
}

bool UAnalytics_ProgressionComponent::RecordFunnelStep(FGameplayTag StepTag)
{
	if (!StepTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("RecordFunnelStep called with an invalid tag on '%s'."),
			*GetNameSafe(GetOwner()));
		return false;
	}

	// Idempotent: only the first reach of a step is recorded.
	if (ReachedSteps.Contains(StepTag))
	{
		return false;
	}
	ReachedSteps.Add(StepTag);

	const UAnalytics_ProgressionDataAsset* Funnel = ResolveFunnelAsset();
	const int32 StepIndex = Funnel ? Funnel->IndexOfStep(StepTag) : INDEX_NONE;
	const bool bKnownStep = StepIndex != INDEX_NONE;
	const bool bIsMilestone = bKnownStep && Funnel->IsMilestoneStep(StepTag);

	// Track deepest funnel depth reached (only known steps contribute to depth).
	if (bKnownStep && StepIndex > DeepestStepIndexReached)
	{
		DeepestStepIndexReached = StepIndex;
	}

	// Build the PII-safe attribute set. Tags/ints/bools only — never a raw id or FText.
	TArray<FSeam_AnalyticsAttr> Attrs;
	Attrs.Reserve(5);
	if (FunnelDefinitionTag.IsValid())
	{
		Attrs.Emplace(GAttr_Funnel, FSeam_NetValue::MakeTag(FunnelDefinitionTag));
	}
	Attrs.Emplace(GAttr_Step, FSeam_NetValue::MakeTag(StepTag));
	if (bKnownStep)
	{
		Attrs.Emplace(GAttr_StepIndex, FSeam_NetValue::MakeInt(StepIndex));
		Attrs.Emplace(GAttr_FunnelDepth, FSeam_NetValue::MakeInt(GetFunnelDepthReached()));
	}
	Attrs.Emplace(GAttr_IsMilestone, FSeam_NetValue::MakeBool(bIsMilestone));

	if (UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem())
	{
		// Recording is consent-gated inside the subsystem.
		Analytics->RecordEvent(AnalyticsProgressionTags::Event_Progression_FunnelStep, Attrs);

		if (bIsMilestone)
		{
			Analytics->RecordEvent(AnalyticsProgressionTags::Event_Progression_Milestone, Attrs);
		}
	}

	// OnMilestoneReached is a GAMEPLAY signal (not telemetry): fire it regardless of analytics
	// availability/consent so game logic can react to milestones even with telemetry off.
	if (bIsMilestone)
	{
		OnMilestoneReached.Broadcast(StepTag, GetFunnelDepthReached());
		UE_LOG(LogDP, Verbose, TEXT("Milestone '%s' reached on '%s' (depth %d)."),
			*StepTag.GetTagName().ToString(), *GetNameSafe(GetOwner()), GetFunnelDepthReached());
	}

	return true;
}

void UAnalytics_ProgressionComponent::EmitPlaytimeHeartbeat()
{
	TimeSinceHeartbeat = 0.0f;

	UAnalytics_Subsystem* Analytics = ResolveAnalyticsSubsystem();
	if (!Analytics)
	{
		return;
	}

	TArray<FSeam_AnalyticsAttr> Attrs;
	Attrs.Reserve(3);
	if (FunnelDefinitionTag.IsValid())
	{
		Attrs.Emplace(GAttr_Funnel, FSeam_NetValue::MakeTag(FunnelDefinitionTag));
	}
	Attrs.Emplace(GAttr_PlaytimeSeconds, FSeam_NetValue::MakeFloat(static_cast<double>(AccumulatedPlaytimeSeconds)));
	if (DeepestStepIndexReached != INDEX_NONE)
	{
		Attrs.Emplace(GAttr_FunnelDepth, FSeam_NetValue::MakeInt(GetFunnelDepthReached()));
	}

	Analytics->RecordEvent(AnalyticsProgressionTags::Event_Progression_PlaytimeHeartbeat, Attrs);
}

void UAnalytics_ProgressionComponent::FlushPlaytimeHeartbeat()
{
	EmitPlaytimeHeartbeat();
}
