// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Loading/Flow_LoadingScreenSubsystem.h"
#include "Loading/Flow_LoadingViewModel.h"
#include "Loading/Streaming/Flow_StreamingLoadCoordinator.h"
#include "Settings/Flow_DeveloperSettings.h"
#include "Flow/Flow_FlowTypes.h"
#include "Flow/Flow_GameFlowSubsystem.h"
#include "Flow/Flow_FlowStateDefinition.h"
#include "DesignPatternsGameFlowModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "MoviePlayer.h"
#include "UObject/UObjectGlobals.h"
#include "Containers/Ticker.h"

// FInstancedStruct: StructUtils plugin on 5.3/5.4, merged into CoreUObject on 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UFlow_LoadingScreenSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Create the loading ViewModel (owning ref keeps it GC-alive while this subsystem lives).
	ViewModel = NewObject<UFlow_LoadingViewModel>(this);

	// Create the owned streaming-load coordinator (aggregates sublevel streaming into the bar). Additive.
	StreamingCoordinator = NewObject<UFlow_StreamingLoadCoordinator>(this);
	StreamingCoordinator->Initialize(this);

	// Wrap the engine's map-change delegates so we cover EVERY travel, even ones we didn't start.
	PreLoadMapHandle  = FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UFlow_LoadingScreenSubsystem::HandlePreLoadMap);
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UFlow_LoadingScreenSubsystem::HandlePostLoadMap);

	UE_LOG(LogDP, Log, TEXT("[Loading] LoadingScreenSubsystem initialized (wrapping PreLoadMap/PostLoadMap)."));
}

void UFlow_LoadingScreenSubsystem::Deinitialize()
{
	CancelPreload();

	if (StreamingCoordinator)
	{
		StreamingCoordinator->Shutdown();
		StreamingCoordinator = nullptr;
	}

	if (PreLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PreLoadMap.Remove(PreLoadMapHandle);
		PreLoadMapHandle.Reset();
	}
	if (PostLoadMapHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
		PostLoadMapHandle.Reset();
	}

	ViewModel = nullptr;
	Super::Deinitialize();
}

void UFlow_LoadingScreenSubsystem::BeginPreload(const TArray<FSoftObjectPath>& TargetAssets, FText StatusLabel, const FString& TargetMapName)
{
	// Reset any prior preload before starting a new one.
	CancelPreload();

	CurrentMapName = TargetMapName;

	if (TargetAssets.Num() == 0)
	{
		// No assets to front-load: just show the screen with an indeterminate fraction; the engine map
		// load (PreLoadMap) will take over from here.
		UpdateProgress(EFlow_LoadingState::Preloading, -1.f, StatusLabel);
		return;
	}

	UpdateProgress(EFlow_LoadingState::Preloading, 0.f, StatusLabel);

	// Async-load the target assets through the shared streamable manager; tick the fraction until done.
	PreloadHandle = StreamableManager.RequestAsyncLoad(TargetAssets, FStreamableDelegate(), FStreamableManager::DefaultAsyncLoadPriority, /*bManageActiveHandle*/ true);

	if (!PreloadHandle.IsValid())
	{
		// Request failed to start (e.g. all paths already loaded): treat as instantly complete.
		UpdateProgress(EFlow_LoadingState::Preloading, 1.f, StatusLabel);
		return;
	}

	PreloadTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UFlow_LoadingScreenSubsystem::TickPreload));
}

void UFlow_LoadingScreenSubsystem::CancelPreload()
{
	if (PreloadTickHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(PreloadTickHandle);
		PreloadTickHandle.Reset();
	}
	if (PreloadHandle.IsValid())
	{
		PreloadHandle->CancelHandle();
		PreloadHandle.Reset();
	}

	// Only force back to Idle if we are not mid map-load (the engine delegates own that lifecycle).
	if (LoadingState == EFlow_LoadingState::Preloading)
	{
		UpdateProgress(EFlow_LoadingState::Idle, -1.f, FText::GetEmpty());
	}
}

void UFlow_LoadingScreenSubsystem::BeginStreamingLoad(const FGameplayTagContainer& Categories, FText Label)
{
	if (StreamingCoordinator)
	{
		StreamingCoordinator->BeginSublevelLoad(Categories, Label);
	}
}

void UFlow_LoadingScreenSubsystem::EndStreamingLoad()
{
	if (StreamingCoordinator)
	{
		StreamingCoordinator->EndSublevelLoad();
	}
}

bool UFlow_LoadingScreenSubsystem::TickPreload(float /*DeltaTime*/)
{
	if (!PreloadHandle.IsValid())
	{
		PreloadTickHandle.Reset();
		return false; // Stop ticking.
	}

	const float Fraction = PreloadHandle->GetProgress();
	UpdateProgress(EFlow_LoadingState::Preloading, Fraction, CurrentLabel);

	if (PreloadHandle->HasLoadCompleted() || Fraction >= 1.f)
	{
		UpdateProgress(EFlow_LoadingState::Preloading, 1.f, CurrentLabel);
		PreloadHandle.Reset();
		PreloadTickHandle.Reset();
		return false; // Stop ticking; the engine map load (if any) continues via PreLoadMap.
	}

	return true; // Keep ticking.
}

void UFlow_LoadingScreenSubsystem::HandlePreLoadMap(const FString& MapName)
{
	CurrentMapName = MapName;

	// Register the engine movie loading screen so the engine blocks on it during the synchronous map
	// flush. Map load gives no fraction, so we present an indeterminate state.
	const FText Label = CurrentLabel.IsEmpty()
		? NSLOCTEXT("DesignPatternsGameFlow", "LoadingLevel", "Loading Level")
		: CurrentLabel;

	SetupEngineLoadingScreen(Label);
	UpdateProgress(EFlow_LoadingState::MapLoading, -1.f, Label);

	UE_LOG(LogDP, Log, TEXT("[Loading] PreLoadMap '%s'."), *MapName);
}

void UFlow_LoadingScreenSubsystem::HandlePostLoadMap(UWorld* /*LoadedWorld*/)
{
	// The map is in. If the now-active flow phase declares streaming content, hand off to the streaming
	// coordinator (which keeps the loading bar up while sublevels stream in) BEFORE we'd otherwise hide.
	// The coordinator re-resolves the streaming adapter per load and is inert when none is present.
	bool bStreamingStarted = false;
	if (StreamingCoordinator)
	{
		if (const UFlow_GameFlowSubsystem* Flow = FDP_SubsystemStatics::GetGameInstanceSubsystem<UFlow_GameFlowSubsystem>(this))
		{
			const FGameplayTag Phase = Flow->GetCurrentPhase();
			if (const UFlow_FlowStateDefinition* Def = Flow->ResolvePhaseDefinitionForLoading(Phase))
			{
				if (!Def->StreamingCategories.IsEmpty())
				{
					const FText Label = CurrentLabel.IsEmpty()
						? NSLOCTEXT("DesignPatternsGameFlow", "StreamingLevel", "Streaming Level")
						: CurrentLabel;
					StreamingCoordinator->BeginSublevelLoad(Def->StreamingCategories, Label);
					bStreamingStarted = true;
				}
			}
		}
	}

	// Finish and hide. The engine auto-hides the movie loading screen; we update our model + UI here. When
	// streaming has begun we keep the bar VISIBLE (the coordinator drives it to completion, then hides it).
	UpdateProgress(EFlow_LoadingState::Finishing, 1.f, CurrentLabel);
	if (!bStreamingStarted)
	{
		UpdateProgress(EFlow_LoadingState::Idle, -1.f, FText::GetEmpty());
		CurrentMapName.Reset();
	}

	UE_LOG(LogDP, Log, TEXT("[Loading] PostLoadMap; %s."),
		bStreamingStarted ? TEXT("streaming sublevels (bar held)") : TEXT("loading screen hidden"));
}

void UFlow_LoadingScreenSubsystem::SetupEngineLoadingScreen(const FText& Label)
{
	// MoviePlayer is not available in all targets (e.g. some commandlets); guard defensively.
	if (!IsMoviePlayerEnabled())
	{
		UE_LOG(LogDP, Verbose, TEXT("[Loading] MoviePlayer disabled; relying on UMG loading UI only."));
		return;
	}

	const UFlow_DeveloperSettings* Settings = UFlow_DeveloperSettings::Get();

	FLoadingScreenAttributes Attributes;
	// Auto-complete when the map finishes loading rather than waiting for input (the common case).
	Attributes.bAutoCompleteWhenLoadingCompletes = !Settings || Settings->bAutoCompleteLoadingScreen;
	// Whether the player can skip the screen with input.
	Attributes.bMoviesAreSkippable = Settings && Settings->bLoadingScreenAllowsSkip;
	// Minimum on-screen time to avoid a one-frame flash.
	Attributes.MinimumLoadingScreenDisplayTime = Settings ? Settings->MinimumLoadingScreenSeconds : 0.f;
	// Keep the engine ticking so a bound UMG widget animates during the flush.
	Attributes.bAllowEngineTick = true;
	Attributes.bAllowInEarlyStartup = false;

	// We do not bind a specific Slate widget here: a project supplies its loading UMG and binds it to the
	// loading ViewModel. The attributes alone instruct the engine to hold a loading screen during the
	// flush. A project wanting a custom Slate widget can set Attributes.WidgetLoadingScreen before this.
	GetMoviePlayer()->SetupLoadingScreen(Attributes);
}

void UFlow_LoadingScreenSubsystem::UpdateProgress(EFlow_LoadingState NewState, float NewProgress, const FText& Label)
{
	LoadingState = NewState;
	CurrentProgress = NewProgress;
	CurrentLabel = Label;

	const bool bVisible = (NewState != EFlow_LoadingState::Idle);

	// Push into the ViewModel for any bound loading UI.
	if (ViewModel)
	{
		ViewModel->SetLoadingState(NewProgress, Label, bVisible);
	}

	// Mirror onto the bus so a HUD or analytics can react without depending on us.
	if (UDP_MessageBusSubsystem* Bus = GetBus())
	{
		FFlow_LoadingProgressPayload Payload;
		Payload.Progress = NewProgress;
		Payload.StatusLabel = Label;
		Payload.TargetMapName = CurrentMapName;
		Bus->BroadcastPayload(FlowTags::Bus_LoadingProgress, FInstancedStruct::Make(Payload), this);
	}
}

UDP_MessageBusSubsystem* UFlow_LoadingScreenSubsystem::GetBus() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
}

FString UFlow_LoadingScreenSubsystem::GetDPDebugString_Implementation() const
{
	const TCHAR* StateStr = TEXT("Idle");
	switch (LoadingState)
	{
	case EFlow_LoadingState::Preloading: StateStr = TEXT("Preloading"); break;
	case EFlow_LoadingState::MapLoading:  StateStr = TEXT("MapLoading");  break;
	case EFlow_LoadingState::Finishing:   StateStr = TEXT("Finishing");   break;
	default: break;
	}

	return FString::Printf(TEXT("Loading: state=%s progress=%s map=%s"),
		StateStr,
		(CurrentProgress < 0.f) ? TEXT("<indeterminate>") : *FString::Printf(TEXT("%.0f%%"), CurrentProgress * 100.f),
		CurrentMapName.IsEmpty() ? TEXT("<none>") : *CurrentMapName);
}
