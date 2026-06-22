// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Loading/Streaming/Flow_StreamingLoadCoordinator.h"
#include "Loading/Flow_LoadingScreenSubsystem.h"
#include "Loading/Flow_LoadingViewModel.h"
#include "Flow/Flow_FlowTypes.h"
#include "Settings/Flow_DeveloperSettings.h"
#include "DesignPatternsGameFlowModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Streaming/Seam_StreamingControl.h"

#include "Engine/World.h"
#include "TimerManager.h"

// FInstancedStruct: StructUtils plugin on 5.3/5.4, merged into CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UFlow_StreamingLoadCoordinator::Initialize(UFlow_LoadingScreenSubsystem* InOwner)
{
	Owner = InOwner;
}

void UFlow_StreamingLoadCoordinator::Shutdown()
{
	EndSublevelLoad();
	Owner.Reset();
}

void UFlow_StreamingLoadCoordinator::BeginSublevelLoad(const FGameplayTagContainer& Categories, FText Label)
{
	// Release any prior aggregate before starting a new one.
	EndSublevelLoad();

	if (Categories.IsEmpty())
	{
		// Nothing to stream: leave the loading screen preload-only (inert).
		return;
	}

	UObject* Streaming = ResolveStreamingForThisLoad();
	if (!Streaming)
	{
		UE_LOG(LogDP, Verbose, TEXT("[Flow][Stream] No ISeam_StreamingControl adapter; streaming aggregate inert."));
		return;
	}

	ActiveCategories = Categories;
	ActiveLabel = Label;
	bAggregating = true;
	LastCombined = 0.f;

	// Request the categories resident through the seam (the adapter wraps the engine streaming).
	ISeam_StreamingControl::Execute_RequestLevelsResident(Streaming, Categories);

	// Begin polling aggregate progress.
	if (UWorld* World = Owner.IsValid() ? Owner->GetWorld() : nullptr)
	{
		const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
		const float Interval = Settings ? FMath::Max(0.02f, Settings->StreamingPollIntervalSeconds) : 0.1f;
		World->GetTimerManager().SetTimer(AggregateTimer,
			FTimerDelegate::CreateUObject(this, &UFlow_StreamingLoadCoordinator::TickAggregate),
			Interval, /*bLoop*/ true);
	}

	UE_LOG(LogDP, Log, TEXT("[Flow][Stream] Begin sublevel load for %d categor(ies)."), Categories.Num());
}

void UFlow_StreamingLoadCoordinator::EndSublevelLoad()
{
	if (UWorld* World = Owner.IsValid() ? Owner->GetWorld() : nullptr)
	{
		World->GetTimerManager().ClearTimer(AggregateTimer);
	}

	if (bAggregating)
	{
		// Release the request so the adapter can let the engine unload these sublevels later if it wants.
		if (UObject* Streaming = ResolveStreamingForThisLoad())
		{
			ISeam_StreamingControl::Execute_ReleaseRequest(Streaming);
		}
	}

	bAggregating = false;
	ActiveCategories.Reset();
	ActiveLabel = FText::GetEmpty();
}

void UFlow_StreamingLoadCoordinator::TickAggregate()
{
	if (!bAggregating)
	{
		return;
	}

	UObject* Streaming = ResolveStreamingForThisLoad();
	if (!Streaming)
	{
		// Adapter vanished (world torn down): stop aggregating gracefully.
		EndSublevelLoad();
		return;
	}

	const float StreamFraction = FMath::Clamp(ISeam_StreamingControl::Execute_GetAggregateProgress(Streaming), 0.f, 1.f);
	const bool bReady = ISeam_StreamingControl::Execute_AreRequestedLevelsReady(Streaming);

	LastCombined = CombineProgress(StreamFraction);

	// Feed the existing loading ViewModel + bus so any bound loading UI mirrors the combined bar.
	if (UFlow_LoadingScreenSubsystem* OwningLoading = Owner.Get())
	{
		if (UFlow_LoadingViewModel* VM = OwningLoading->GetViewModel())
		{
			VM->SetLoadingState(LastCombined, ActiveLabel, /*bVisible*/ true);
		}
	}

	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		FFlow_LoadingProgressPayload Payload;
		Payload.Progress = LastCombined;
		Payload.StatusLabel = ActiveLabel;
		Bus->BroadcastPayload(FlowTags::Bus_LoadingProgress, FInstancedStruct::Make(Payload), this);
	}

	if (bReady && StreamFraction >= 1.f)
	{
		UE_LOG(LogDP, Log, TEXT("[Flow][Stream] Sublevels resident; aggregate load complete."));
		EndSublevelLoad();
	}
}

float UFlow_StreamingLoadCoordinator::CombineProgress(float StreamingFraction) const
{
	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();
	const float Weight = Settings ? FMath::Clamp(Settings->PreloadVsStreamWeight, 0.f, 1.f) : 0.5f;

	// The owner's preload fraction (-1 when indeterminate); treat indeterminate as fully-preloaded so the
	// streaming fraction drives the bar in a pure-streaming phase.
	float PreloadFraction = 1.f;
	if (const UFlow_LoadingScreenSubsystem* OwningLoading = Owner.Get())
	{
		const float P = OwningLoading->GetProgress();
		PreloadFraction = (P < 0.f) ? 1.f : FMath::Clamp(P, 0.f, 1.f);
	}

	// Weighted blend: Weight toward preload, (1-Weight) toward streaming.
	return FMath::Clamp(Weight * PreloadFraction + (1.f - Weight) * StreamingFraction, 0.f, 1.f);
}

UObject* UFlow_StreamingLoadCoordinator::ResolveStreamingForThisLoad() const
{
	// RE-RESOLVE every call — the LevelDirector adapter is world-lifetime; a cached ptr dangles after travel.
	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return nullptr;
	}
	UObject* Obj = Locator->ResolveService(FlowTags::Service_StreamingControl);
	return (Obj && Obj->Implements<USeam_StreamingControl>()) ? Obj : nullptr;
}

UDP_MessageBusSubsystem* UFlow_StreamingLoadCoordinator::GetBus() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
}
