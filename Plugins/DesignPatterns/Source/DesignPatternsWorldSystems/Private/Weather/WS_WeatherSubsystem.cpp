// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Weather/WS_WeatherSubsystem.h"
#include "Weather/WS_WeatherStateDataAsset.h"
#include "Weather/WS_WeatherMessages.h"
#include "Settings/WS_DeveloperSettings.h"
#include "DesignPatternsWorldSystemsModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Clock/Seam_SimClock.h"

#include "Engine/World.h"
#include "Engine/AssetManager.h"
#include "EngineUtils.h"
#include "TimerManager.h"

// FInstancedStruct version-gated include (used to build/read bus payloads).
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UWS_WeatherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Force the GI-scoped dependencies to exist before we register/listen/resolve.
	Collection.InitializeDependency(UDP_ServiceLocatorSubsystem::StaticClass());
	Collection.InitializeDependency(UDP_DataRegistrySubsystem::StaticClass());
	Collection.InitializeDependency(UDP_MessageBusSubsystem::StaticClass());

	RegisterDefaultStatesFromSettings();
	RegisterAsService();
	BindBusListeners();

	// The server spawns the single replicated carrier; clients will pick up the replicated one lazily.
	if (HasWorldAuthority())
	{
		EnsureCarrierSpawnedOnAuthority();

		// Enter the project's default weather state (if any) so a fresh world is not blank.
		if (const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get())
		{
			if (Settings->DefaultWeatherStateTag.IsValid())
			{
				SetWeather(Settings->DefaultWeatherStateTag, /*bInstant*/ true, /*bForceRestart*/ false);
			}
		}
	}

	// Bind to whatever carrier exists in this world now (and re-check after the default apply above).
	EnsureCarrierBound();

	UE_LOG(LogDP, Log, TEXT("WS_WeatherSubsystem initialized (authority=%d)."), HasWorldAuthority() ? 1 : 0);
}

void UWS_WeatherSubsystem::Deinitialize()
{
	// Stop the blend timer we own.
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(UpdateTimerHandle);
	}

	// Stop our bus subscriptions.
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}

	// Return any looping weather VFX so it does not outlive the world.
	StopActiveWeatherVfx();

	UnregisterAsService();

	CachedSimClock.Reset();
	Carrier.Reset();
	ActiveStateAsset.Reset();

	Super::Deinitialize();
}

// =====================================================================================================
// Setup
// =====================================================================================================

void UWS_WeatherSubsystem::RegisterDefaultStatesFromSettings()
{
	const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();
	if (!Settings)
	{
		// Defensive: with no CDO there is nothing to pre-register; states still resolve lazily on demand.
		return;
	}

	// Pre-resolve the soft asset ids so the registry indexes them; we do not force-load here (FindByTag
	// loads on first real use). Touching the soft path is enough to make the asset discoverable.
	for (const TSoftObjectPtr<UWS_WeatherStateDataAsset>& Soft : Settings->DefaultWeatherStates)
	{
		if (!Soft.IsNull())
		{
			// Synchronously load the lightweight state asset so its StateTag is in the registry index.
			if (UWS_WeatherStateDataAsset* Asset = Soft.LoadSynchronous())
			{
				UE_LOG(LogDP, Verbose, TEXT("WS_WeatherSubsystem: pre-registered weather state '%s'."),
					*Asset->StateTag.ToString());
			}
		}
	}
}

void UWS_WeatherSubsystem::RegisterAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// World-lifetime provider registered WEAKLY so a torn-down world cannot leak through the
		// GI-scoped locator. Allow override so a re-created world replaces a stale binding.
		Locator->RegisterService(WorldSystemsNativeTags::Service_Weather, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/ true);
	}
}

void UWS_WeatherSubsystem::UnregisterAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// Only drop the binding if it still points at us (a newer world may already have overridden it).
		if (Locator->ResolveService(WorldSystemsNativeTags::Service_Weather) == this)
		{
			Locator->UnregisterService(WorldSystemsNativeTags::Service_Weather);
		}
	}
}

void UWS_WeatherSubsystem::BindBusListeners()
{
	if (bBusBound)
	{
		return;
	}

	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->ListenNative(
			WorldSystemsNativeTags::Bus_RequestWeather,
			[this](const FDP_Message& Message) { HandleRequestWeatherMessage(Message); },
			this,
			EDP_MessageMatch::ExactOrChild);
		bBusBound = true;
	}
}

void UWS_WeatherSubsystem::EnsureCarrierSpawnedOnAuthority()
{
	if (!HasWorldAuthority() || Carrier.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient; // weather carrier is runtime-only, never saved into the map.

	AWS_WeatherCarrier* Spawned = World->SpawnActor<AWS_WeatherCarrier>(
		AWS_WeatherCarrier::StaticClass(), FTransform::Identity, Params);
	Carrier = Spawned;

	if (!Spawned)
	{
		UE_LOG(LogDP, Warning, TEXT("WS_WeatherSubsystem: failed to spawn weather carrier; weather will be inert."));
	}
}

void UWS_WeatherSubsystem::EnsureCarrierBound()
{
	// Find whichever carrier is in this world (the server's spawned one, or a replicated one on a client).
	if (!Carrier.IsValid())
	{
		if (const UWorld* World = GetWorld())
		{
			for (TActorIterator<AWS_WeatherCarrier> It(World); It; ++It)
			{
				Carrier = *It;
				break;
			}
		}
	}

	if (AWS_WeatherCarrier* C = Carrier.Get())
	{
		// Bind exactly once (AddUnique guards against double-binding across re-entry).
		C->OnStateChanged.AddUniqueDynamic(this, &UWS_WeatherSubsystem::HandleCarrierStateChanged);

		// If the carrier already carries a state (e.g. a client that joined mid-game), react now.
		const FGameplayTag Existing = C->GetCurrentState();
		if (Existing.IsValid() && Existing != LastReactedState)
		{
			ApplyStateLocally(Existing, /*bInstant*/ true);
		}
	}
}

// =====================================================================================================
// Authority API
// =====================================================================================================

bool UWS_WeatherSubsystem::SetWeather(FGameplayTag StateTag, bool bInstant, bool bForceRestart)
{
	// Authority gate at the very top — clients route intent via the bus / a player-owned component.
	if (!HasWorldAuthority())
	{
		UE_LOG(LogDP, Verbose, TEXT("WS_WeatherSubsystem::SetWeather ignored on client."));
		return false;
	}

	if (!StateTag.IsValid())
	{
		return false;
	}

	// Make sure we have a carrier to drive (lazily spawn if a late call beat Initialize's spawn).
	EnsureCarrierSpawnedOnAuthority();
	AWS_WeatherCarrier* C = Carrier.Get();
	if (!C)
	{
		UE_LOG(LogDP, Warning, TEXT("WS_WeatherSubsystem::SetWeather: no carrier; cannot apply '%s'."),
			*StateTag.ToString());
		return false;
	}

	// No-op if already the active state and not forcing a restart.
	if (!bForceRestart && C->GetCurrentState() == StateTag)
	{
		return false;
	}

	// Resolve the state asset up front so an unknown tag fails fast (and so the bInstant flag can be
	// stashed for the local reaction — the carrier replicates only the tag).
	if (!ResolveState(StateTag))
	{
		UE_LOG(LogDP, Warning, TEXT("WS_WeatherSubsystem::SetWeather: no weather-state asset for '%s'."),
			*StateTag.ToString());
		return false;
	}

	// Remember the requested blend mode for THIS authority's local reaction; the carrier's OnStateChanged
	// fires synchronously inside AuthSetState on the server, where we read this.
	bPendingInstant = bInstant;

	// Drive the replicated carrier; its OnStateChanged fans to HandleCarrierStateChanged here and on clients.
	return C->AuthSetState(StateTag);
}

// =====================================================================================================
// Reactions
// =====================================================================================================

void UWS_WeatherSubsystem::HandleCarrierStateChanged(AWS_WeatherCarrier* InCarrier, FGameplayTag NewState)
{
	if (InCarrier != Carrier.Get())
	{
		// Ignore a stray carrier we are not bound to.
		return;
	}

	// On the server bPendingInstant was set by SetWeather; on clients it is always a blended apply unless
	// this is the first observed state (cut in so a joining client is not mid-fade from nothing).
	const bool bInstant = HasWorldAuthority() ? bPendingInstant : !LastReactedState.IsValid();
	bPendingInstant = false;

	ApplyStateLocally(NewState, bInstant);
}

void UWS_WeatherSubsystem::ApplyStateLocally(const FGameplayTag& NewState, bool bInstant)
{
	const FGameplayTag PreviousState = LastReactedState;
	if (NewState == PreviousState && bTransitionActive == false && ActiveVfxHandle.IsValid())
	{
		// Already settled on this exact state with VFX live — nothing to redo.
		return;
	}

	UWS_WeatherStateDataAsset* Asset = ResolveState(NewState);
	const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();

	// ---- Compute the blend ----
	const float TargetIntensity = Asset ? FMath::Clamp(Asset->Intensity, 0.f, 1.f) : 0.f;

	float TransitionSeconds = 0.f;
	if (!bInstant)
	{
		if (Asset && Asset->HasExplicitTransition())
		{
			TransitionSeconds = FMath::Max(0.f, Asset->TransitionSeconds);
		}
		else if (Settings)
		{
			TransitionSeconds = FMath::Max(0.f, Settings->DefaultWeatherTransitionSeconds);
		}
		else
		{
			// Defensive: no asset transition and no CDO — fall back to an instant cut rather than guess.
			TransitionSeconds = 0.f;
		}
	}

	BlendStartIntensity = CurrentIntensity;
	BlendTargetIntensity = TargetIntensity;
	BlendDuration = TransitionSeconds;
	BlendElapsed = 0.f;

	if (TransitionSeconds <= KINDA_SMALL_NUMBER)
	{
		// Instant cut.
		CurrentIntensity = TargetIntensity;
		bTransitionActive = false;
	}
	else
	{
		bTransitionActive = true;
		EnsureUpdateTimer();
	}

	ActiveStateAsset = Asset;

	// ---- Drive the cosmetic VFX (local only) ----
	// Stop the old looping weather effect, then start the new state's particle (if any).
	StopActiveWeatherVfx();
	if (Asset && Asset->ParticleVfxTag.IsValid())
	{
		if (TScriptInterface<ISeam_VfxController> Vfx = ResolveVfxController())
		{
			// Weather particles are world-global ambience — spawn at the local viewer's reference point
			// (origin); the cosmetic system itself is responsible for following the camera. We keep the
			// handle so we can stop it on the next change.
			ActiveVfxHandle = ISeam_VfxController::Execute_SpawnVfxAtLocation(
				Vfx.GetObject(), Asset->ParticleVfxTag, FVector::ZeroVector, FRotator::ZeroRotator);
		}
		// No controller resolved -> documented inert default: no particles, but state/audio still apply.
	}

	// ---- Broadcast the change over the bus (audio/lighting consumers) ----
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FWS_WeatherChangedMessage Payload;
		Payload.NewStateTag = NewState;
		Payload.PreviousStateTag = PreviousState;
		Payload.TargetIntensity = TargetIntensity;
		Payload.WindVector = Asset ? Asset->WindVector : FVector::ZeroVector;
		Payload.ParticleVfxTag = Asset ? Asset->ParticleVfxTag : FGameplayTag();
		Payload.AmbientSoundTag = Asset ? Asset->AmbientSoundTag : FGameplayTag();
		Payload.TransitionSeconds = TransitionSeconds;

		Bus->BroadcastPayload(
			WorldSystemsNativeTags::Bus_WeatherChanged,
			FInstancedStruct::Make(Payload),
			this);
	}

	LastReactedState = NewState;

	// ---- Local delegate for same-world gameplay/UI ----
	OnWeatherChanged.Broadcast(NewState, PreviousState);

	UE_LOG(LogDP, Log, TEXT("WS_WeatherSubsystem: weather '%s' -> '%s' (target=%.2f, blend=%.2fs%s)."),
		*PreviousState.ToString(), *NewState.ToString(), TargetIntensity, TransitionSeconds,
		bInstant ? TEXT(", instant") : TEXT(""));
}

void UWS_WeatherSubsystem::HandleRequestWeatherMessage(const FDP_Message& Message)
{
	// Only authority acts on a request; on clients the resulting state replicates back via the carrier.
	if (!HasWorldAuthority())
	{
		return;
	}

	if (const FWS_RequestWeatherMessage* Request = Message.Payload.GetPtr<FWS_RequestWeatherMessage>())
	{
		if (Request->RequestedStateTag.IsValid())
		{
			SetWeather(Request->RequestedStateTag, Request->bInstant, /*bForceRestart*/ false);
		}
	}
}

// =====================================================================================================
// Resolution helpers
// =====================================================================================================

UWS_WeatherStateDataAsset* UWS_WeatherSubsystem::ResolveState(const FGameplayTag& StateTag) const
{
	if (!StateTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Registry->Find<UWS_WeatherStateDataAsset>(StateTag);
	}
	return nullptr;
}

TScriptInterface<ISeam_VfxController> UWS_WeatherSubsystem::ResolveVfxController() const
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Provider = Locator->ResolveService(WorldSystemsNativeTags::Service_Vfx))
		{
			if (Provider->GetClass()->ImplementsInterface(USeam_VfxController::StaticClass()))
			{
				TScriptInterface<ISeam_VfxController> Result;
				Result.SetObject(Provider);
				Result.SetInterface(Cast<ISeam_VfxController>(Provider));
				return Result;
			}
		}
	}
	return TScriptInterface<ISeam_VfxController>();
}

TScriptInterface<ISeam_SimClock> UWS_WeatherSubsystem::ResolveSimClock() const
{
	// Already-resolved live clock? Use it.
	if (CachedSimClock.IsValid())
	{
		TScriptInterface<ISeam_SimClock> Result;
		Result.SetObject(CachedSimClock.GetObject());
		Result.SetInterface(CachedSimClock.Get());
		return Result;
	}

	// Otherwise try to resolve from the locator under the configured key (defaults documented in settings).
	const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();
	const FGameplayTag ClockKey = Settings ? Settings->SimClockServiceKey : FGameplayTag();
	if (ClockKey.IsValid())
	{
		if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			if (UObject* Provider = Locator->ResolveService(ClockKey))
			{
				if (Provider->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
				{
					// Cache weakly so a torn-down clock world does not dangle.
					CachedSimClock = TWeakInterfacePtr<ISeam_SimClock>(*Cast<ISeam_SimClock>(Provider));

					TScriptInterface<ISeam_SimClock> Result;
					Result.SetObject(Provider);
					Result.SetInterface(Cast<ISeam_SimClock>(Provider));
					return Result;
				}
			}
		}
	}
	// Unresolved -> documented inert default: weather blends in real time.
	return TScriptInterface<ISeam_SimClock>();
}

// =====================================================================================================
// Transition stepping
// =====================================================================================================

void UWS_WeatherSubsystem::EnsureUpdateTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (World->GetTimerManager().IsTimerActive(UpdateTimerHandle))
	{
		return;
	}

	const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();
	// Defensive lower bound so a misconfigured 0 cannot busy-loop the timer.
	const float Interval = Settings ? FMath::Max(0.02f, Settings->WeatherUpdateInterval) : 0.2f;

	World->GetTimerManager().SetTimer(
		UpdateTimerHandle, this, &UWS_WeatherSubsystem::StepTransition, Interval, /*bLoop*/ true);
}

void UWS_WeatherSubsystem::StepTransition()
{
	if (!bTransitionActive)
	{
		// Nothing to blend — stop the recurring timer to idle cleanly.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(UpdateTimerHandle);
		}
		return;
	}

	const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();
	float DeltaSeconds = Settings ? FMath::Max(0.02f, Settings->WeatherUpdateInterval) : 0.2f;

	// Honour the shared simulation clock if one is published: scale by its time scale and freeze on pause.
	if (TScriptInterface<ISeam_SimClock> Clock = ResolveSimClock())
	{
		if (ISeam_SimClock::Execute_IsPaused(Clock.GetObject()))
		{
			return; // paused — do not advance the blend.
		}
		const double Scale = ISeam_SimClock::Execute_GetTimeScale(Clock.GetObject());
		DeltaSeconds *= static_cast<float>(FMath::Max(0.0, Scale));
	}

	BlendElapsed += DeltaSeconds;

	const float Alpha = (BlendDuration > KINDA_SMALL_NUMBER)
		? FMath::Clamp(BlendElapsed / BlendDuration, 0.f, 1.f)
		: 1.f;

	CurrentIntensity = FMath::Lerp(BlendStartIntensity, BlendTargetIntensity, Alpha);

	if (Alpha >= 1.f)
	{
		CurrentIntensity = BlendTargetIntensity;
		bTransitionActive = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(UpdateTimerHandle);
		}
	}
}

void UWS_WeatherSubsystem::StopActiveWeatherVfx()
{
	if (!ActiveVfxHandle.IsValid())
	{
		return;
	}
	if (TScriptInterface<ISeam_VfxController> Vfx = ResolveVfxController())
	{
		ISeam_VfxController::Execute_StopVfx(Vfx.GetObject(), ActiveVfxHandle);
	}
	ActiveVfxHandle = FSeam_VfxHandle();
}

// =====================================================================================================
// Reads / debug
// =====================================================================================================

FGameplayTag UWS_WeatherSubsystem::GetCurrentWeatherState() const
{
	if (const AWS_WeatherCarrier* C = Carrier.Get())
	{
		return C->GetCurrentState();
	}
	return LastReactedState;
}

FString UWS_WeatherSubsystem::GetDPDebugString_Implementation() const
{
	const FGameplayTag State = GetCurrentWeatherState();
	return FString::Printf(TEXT("Weather: %s  intensity=%.2f%s  vfx=%s  carrier=%s"),
		State.IsValid() ? *State.ToString() : TEXT("<none>"),
		CurrentIntensity,
		bTransitionActive ? TEXT(" (blending)") : TEXT(""),
		ActiveVfxHandle.IsValid() ? TEXT("live") : TEXT("none"),
		Carrier.IsValid() ? TEXT("bound") : TEXT("missing"));
}
