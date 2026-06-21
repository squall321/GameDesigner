// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Vfx/WS_VfxManagerSubsystem.h"
#include "Vfx/WS_VfxBankDataAsset.h"
#include "Vfx/WS_VfxCarrier.h"
#include "Settings/WS_DeveloperSettings.h"
#include "DesignPatternsWorldSystemsModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "Pool/DPObjectPoolSubsystem.h"
#include "Pool/DPPoolTypes.h"

#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "FXSystemAsset.h"               // UFXSystemAsset (Engine module).
#include "Particles/FXSystemComponent.h" // UFXSystemComponent (Engine module).
#include "TimerManager.h"
#include "Misc/App.h"

// =====================================================================================================
// Lifecycle
// =====================================================================================================

void UWS_VfxManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Force the locator to exist before we register ourselves as the ISeam_VfxController provider.
	Collection.InitializeDependency(UDP_ServiceLocatorSubsystem::StaticClass());

	// Detect rendering availability once. On a dedicated server / -nullrhi there is no FX, so every spawn
	// becomes a guarded no-op while handle bookkeeping still behaves.
	const UGameInstance* GI = GetGameInstance();
	const UWorld* World = GI ? GI->GetWorld() : nullptr;
	const bool bDedicated = (World && World->GetNetMode() == NM_DedicatedServer);
	bFxAvailable = !bDedicated && (GEngine != nullptr) && !IsRunningDedicatedServer() && FApp::CanEverRender();

	RegisterDefaultBanksFromSettings();
	RegisterAsService();

	UE_LOG(LogDP, Log, TEXT("WS_VfxManagerSubsystem initialized (fx %s, %d bank(s))."),
		bFxAvailable ? TEXT("available") : TEXT("unavailable"), RegisteredBanks.Num());
}

void UWS_VfxManagerSubsystem::Deinitialize()
{
	// Stop the sweep timer (it lives on a world; clear from whichever world we can reach).
	if (const UGameInstance* GI = GetGameInstance())
	{
		if (UWorld* World = GI->GetWorld())
		{
			World->GetTimerManager().ClearTimer(SweepTimerHandle);
		}
	}

	// Recycle every tracked effect so no pooled carrier leaks across travel.
	TArray<int64> Ids;
	TrackedEffects.GetKeys(Ids);
	for (int64 Id : Ids)
	{
		ReleaseTracked(Id);
	}
	TrackedEffects.Reset();

	UnregisterAsService();

	EntryIndex.Reset();
	RegisteredBanks.Reset();

	Super::Deinitialize();
}

// =====================================================================================================
// Setup
// =====================================================================================================

void UWS_VfxManagerSubsystem::RegisterDefaultBanksFromSettings()
{
	const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();
	if (!Settings)
	{
		return; // Defensive: no CDO -> no default banks; runtime RegisterBank still works.
	}

	for (const TSoftObjectPtr<UWS_VfxBankDataAsset>& Soft : Settings->DefaultVfxBanks)
	{
		if (!Soft.IsNull())
		{
			if (UWS_VfxBankDataAsset* Bank = Soft.LoadSynchronous())
			{
				RegisterBank(Bank);
			}
		}
	}
}

void UWS_VfxManagerSubsystem::RegisterBank(UWS_VfxBankDataAsset* Bank)
{
	if (!Bank)
	{
		return;
	}
	if (RegisteredBanks.Contains(Bank))
	{
		return; // Already registered.
	}

	RegisteredBanks.Add(Bank);

	int32 Added = 0;
	for (const FWS_VfxEntry& Entry : Bank->Entries)
	{
		if (!Entry.VfxTag.IsValid())
		{
			continue;
		}
		if (EntryIndex.Contains(Entry.VfxTag))
		{
			UE_LOG(LogDP, Warning, TEXT("WS_VfxManagerSubsystem: duplicate VFX tag '%s'; keeping the first."),
				*Entry.VfxTag.ToString());
			continue;
		}
		// Point into the bank's array (the bank is strong-held in RegisteredBanks, so this stays valid).
		EntryIndex.Add(Entry.VfxTag, &Entry);
		++Added;
	}

	UE_LOG(LogDP, Verbose, TEXT("WS_VfxManagerSubsystem: registered bank '%s' (%d effect(s))."),
		*Bank->GetName(), Added);
}

void UWS_VfxManagerSubsystem::RegisterAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// GI-scoped provider whose lifetime matches the locator's; register strong-owned so the seam stays
		// resolvable for the GameInstance's life. Allow override so a replacing module wins cleanly.
		Locator->RegisterService(WorldSystemsNativeTags::Service_Vfx, this,
			EDP_ServiceLifetime::StrongOwned, /*bAllowOverride*/ true);
	}
}

void UWS_VfxManagerSubsystem::UnregisterAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (Locator->ResolveService(WorldSystemsNativeTags::Service_Vfx) == this)
		{
			Locator->UnregisterService(WorldSystemsNativeTags::Service_Vfx);
		}
	}
}

// =====================================================================================================
// ISeam_VfxController
// =====================================================================================================

FSeam_VfxHandle UWS_VfxManagerSubsystem::SpawnVfxAtLocation_Implementation(FGameplayTag VfxTag, FVector Location, FRotator Rotation)
{
	const FTransform WorldTransform(Rotation, Location, FVector::OneVector);
	return SpawnInternal(VfxTag, WorldTransform, /*AttachTo*/ nullptr, NAME_None, /*bAttached*/ false);
}

FSeam_VfxHandle UWS_VfxManagerSubsystem::SpawnVfxAttached_Implementation(FGameplayTag VfxTag, USceneComponent* AttachTo, FName Socket)
{
	// If no attach target is given, degrade to a world-space spawn at origin rather than failing.
	const FTransform WorldTransform = AttachTo
		? AttachTo->GetSocketTransform(Socket)
		: FTransform::Identity;
	return SpawnInternal(VfxTag, WorldTransform, AttachTo, Socket, /*bAttached*/ true);
}

void UWS_VfxManagerSubsystem::StopVfx_Implementation(FSeam_VfxHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}
	ReleaseTracked(Handle.Id);
}

// =====================================================================================================
// Spawn core
// =====================================================================================================

FSeam_VfxHandle UWS_VfxManagerSubsystem::SpawnInternal(FGameplayTag VfxTag, const FTransform& WorldTransform,
	USceneComponent* AttachTo, FName Socket, bool bAttached)
{
	if (!VfxTag.IsValid())
	{
		return FSeam_VfxHandle();
	}

	const FWS_VfxEntry* Entry = FindEntry(VfxTag);
	if (!Entry)
	{
		UE_LOG(LogDP, Verbose, TEXT("WS_VfxManagerSubsystem: no VFX registered for tag '%s'."), *VfxTag.ToString());
		return FSeam_VfxHandle();
	}

	const bool bTracked = bAttached || Entry->bLooping;

	// No rendering -> guarded no-op. Tracked effects still get a coherent handle so StopVfx is balanced;
	// the tracked slot simply has no carrier and is reclaimed immediately by the next budget/sweep pass.
	if (!bFxAvailable)
	{
		if (!bTracked)
		{
			return FSeam_VfxHandle();
		}
		FSeam_VfxHandle Handle;
		Handle.Id = AllocateHandleId();
		FWS_TrackedVfx Tracked;
		Tracked.HandleId = Handle.Id;
		Tracked.VfxTag = VfxTag;
		Tracked.bLooping = Entry->bLooping;
		Tracked.bPooled = Entry->bPooled;
		Tracked.SpawnRealTime = FPlatformTime::Seconds();
		TrackedEffects.Add(Handle.Id, Tracked);
		return Handle;
	}

	UFXSystemAsset* System = LoadSystem(*Entry);
	if (!System)
	{
		UE_LOG(LogDP, Warning, TEXT("WS_VfxManagerSubsystem: failed to load system for VFX '%s'."), *VfxTag.ToString());
		return FSeam_VfxHandle();
	}

	AWS_VfxCarrier* Carrier = AcquireCarrier(*Entry);
	if (!Carrier)
	{
		return FSeam_VfxHandle();
	}

	// Place / attach the carrier, then activate the effect.
	if (bAttached && AttachTo)
	{
		Carrier->AttachToComponent(AttachTo, FAttachmentTransformRules::SnapToTargetNotIncludingScale, Socket);
	}
	else
	{
		Carrier->SetActorTransform(WorldTransform);
	}

	const float Scale = FMath::Max(0.01f, Entry->Scale);
	const bool bActivated = Carrier->ActivateSystem(System, Scale, /*bAutoActivate*/ true);
	if (!bActivated)
	{
		// Could not start (null component) -> recycle the carrier and report failure.
		ReleaseCarrier(Carrier, Entry->bPooled);
		return FSeam_VfxHandle();
	}

	if (!bTracked)
	{
		// One-shot at a world location: track it internally (so the sweep can reclaim a stuck one) but
		// hand back an INVALID handle per the seam contract (the caller cannot stop a fire-and-forget).
		const int64 OneShotId = AllocateHandleId();

		FWS_TrackedVfx Tracked;
		Tracked.HandleId = OneShotId;
		Tracked.Carrier = Carrier;
		Tracked.VfxTag = VfxTag;
		Tracked.bLooping = false;
		Tracked.bPooled = Entry->bPooled;
		Tracked.SpawnRealTime = FPlatformTime::Seconds();
		TrackedEffects.Add(OneShotId, Tracked);

		// Recycle (and untrack) on natural completion; the safety-net sweep covers systems that never fire.
		Carrier->OnEffectFinished.AddLambda([this, OneShotId](AWS_VfxCarrier* /*Finished*/)
		{
			ReleaseTracked(OneShotId);
		});

		EnsureSweepTimer();
		EnforceTrackedBudget();

		return FSeam_VfxHandle();
	}

	// Tracked (attached or looping): retain a handle so the caller can StopVfx.
	FSeam_VfxHandle Handle;
	Handle.Id = AllocateHandleId();

	FWS_TrackedVfx Tracked;
	Tracked.HandleId = Handle.Id;
	Tracked.Carrier = Carrier;
	Tracked.VfxTag = VfxTag;
	Tracked.bLooping = Entry->bLooping;
	Tracked.bPooled = Entry->bPooled;
	Tracked.SpawnRealTime = FPlatformTime::Seconds();
	TrackedEffects.Add(Handle.Id, Tracked);

	// For a non-looping attached effect, also recycle on natural completion.
	if (!Entry->bLooping)
	{
		const int64 HandleId = Handle.Id;
		Carrier->OnEffectFinished.AddLambda([this, HandleId](AWS_VfxCarrier* /*Finished*/)
		{
			ReleaseTracked(HandleId);
		});
	}

	EnsureSweepTimer();
	EnforceTrackedBudget();
	return Handle;
}

const FWS_VfxEntry* UWS_VfxManagerSubsystem::FindEntry(FGameplayTag VfxTag) const
{
	if (const FWS_VfxEntry* const* Found = EntryIndex.Find(VfxTag))
	{
		return *Found;
	}
	return nullptr;
}

UFXSystemAsset* UWS_VfxManagerSubsystem::LoadSystem(const FWS_VfxEntry& Entry) const
{
	if (Entry.System.IsNull())
	{
		return nullptr;
	}
	// Synchronous load on first use; subsequent uses hit the resident pointer cheaply.
	return Entry.System.LoadSynchronous();
}

bool UWS_VfxManagerSubsystem::HasVfx(FGameplayTag VfxTag) const
{
	return EntryIndex.Contains(VfxTag);
}

// =====================================================================================================
// Carrier acquire / release (core pool)
// =====================================================================================================

AWS_VfxCarrier* UWS_VfxManagerSubsystem::AcquireCarrier(const FWS_VfxEntry& Entry)
{
	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	if (Entry.bPooled)
	{
		// Pool is world-scoped; resolve it from the current world.
		if (UDP_ObjectPoolSubsystem* Pool = World->GetSubsystem<UDP_ObjectPoolSubsystem>())
		{
			// Lazily register + warm the pool the first time we see this carrier class.
			if (!Pool->HasPool(AWS_VfxCarrier::StaticClass()))
			{
				FDP_PoolConfig Config(AWS_VfxCarrier::StaticClass());
				Pool->RegisterPool(Config);

				const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();
				int32 Warmup = Entry.PoolWarmup;
				if (Warmup < 0)
				{
					Warmup = Settings ? FMath::Max(0, Settings->DefaultVfxPoolWarmup) : 0;
				}
				if (Warmup > 0)
				{
					Pool->WarmupAsync(AWS_VfxCarrier::StaticClass(), Warmup);
				}
			}

			AActor* Actor = Pool->AcquireActor(AWS_VfxCarrier::StaticClass(), FTransform::Identity, nullptr);
			return Cast<AWS_VfxCarrier>(Actor);
		}
	}

	// Non-pooled (or no pool subsystem): plain transient spawn.
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	return World->SpawnActor<AWS_VfxCarrier>(AWS_VfxCarrier::StaticClass(), FTransform::Identity, Params);
}

void UWS_VfxManagerSubsystem::ReleaseCarrier(AWS_VfxCarrier* CarrierActor, bool bPooled)
{
	if (!IsValid(CarrierActor))
	{
		return;
	}

	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;

	if (bPooled && World)
	{
		if (UDP_ObjectPoolSubsystem* Pool = World->GetSubsystem<UDP_ObjectPoolSubsystem>())
		{
			// Detach so it returns to the pool clean; the pool's OnReturnedToPool hook tears down the effect.
			CarrierActor->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
			Pool->Release(CarrierActor);
			return;
		}
	}

	// Non-pooled / no pool: tear down and destroy.
	CarrierActor->DeactivateSystem();
	CarrierActor->Destroy();
}

// =====================================================================================================
// Tracking
// =====================================================================================================

int64 UWS_VfxManagerSubsystem::AllocateHandleId()
{
	// Never hand out 0 (reserved for an invalid handle).
	int64 Id = NextHandleId++;
	if (Id == 0)
	{
		Id = NextHandleId++;
	}
	return Id;
}

FWS_TrackedVfx* UWS_VfxManagerSubsystem::FindTracked(int64 HandleId)
{
	return TrackedEffects.Find(HandleId);
}

void UWS_VfxManagerSubsystem::ReleaseTracked(int64 HandleId)
{
	FWS_TrackedVfx Tracked;
	if (!TrackedEffects.RemoveAndCopyValue(HandleId, Tracked))
	{
		return;
	}
	if (AWS_VfxCarrier* C = Tracked.Carrier.Get())
	{
		ReleaseCarrier(C, Tracked.bPooled);
	}
}

void UWS_VfxManagerSubsystem::EnforceTrackedBudget()
{
	const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();
	// Defensive positive lower bound so a misconfigured cap cannot reclaim everything every spawn.
	const int32 MaxTracked = Settings ? FMath::Max(1, Settings->MaxTrackedVfx) : 128;

	while (TrackedEffects.Num() > MaxTracked)
	{
		// Reclaim the oldest tracked entry (smallest SpawnRealTime).
		int64 OldestId = 0;
		double OldestTime = TNumericLimits<double>::Max();
		for (const TPair<int64, FWS_TrackedVfx>& Pair : TrackedEffects)
		{
			if (Pair.Value.SpawnRealTime < OldestTime)
			{
				OldestTime = Pair.Value.SpawnRealTime;
				OldestId = Pair.Key;
			}
		}
		if (OldestId == 0)
		{
			break;
		}
		ReleaseTracked(OldestId);
	}
}

void UWS_VfxManagerSubsystem::EnsureSweepTimer()
{
	const UGameInstance* GI = GetGameInstance();
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	if (World->GetTimerManager().IsTimerActive(SweepTimerHandle))
	{
		return;
	}

	const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();
	const float ReclaimSeconds = Settings ? FMath::Max(0.5f, Settings->OneShotReclaimSeconds) : 30.f;
	// Sweep at a fraction of the reclaim window so stuck one-shots are caught reasonably promptly.
	const float SweepInterval = FMath::Max(0.5f, ReclaimSeconds * 0.5f);

	World->GetTimerManager().SetTimer(
		SweepTimerHandle, this, &UWS_VfxManagerSubsystem::SweepStaleOneShots, SweepInterval, /*bLoop*/ true);
}

void UWS_VfxManagerSubsystem::SweepStaleOneShots()
{
	const UWS_DeveloperSettings* Settings = UWS_DeveloperSettings::Get();
	const double ReclaimSeconds = Settings ? FMath::Max(0.5f, Settings->OneShotReclaimSeconds) : 30.0;
	const double Now = FPlatformTime::Seconds();

	TArray<int64> ToReclaim;
	for (const TPair<int64, FWS_TrackedVfx>& Pair : TrackedEffects)
	{
		// Looping/attached effects are never auto-reclaimed (only StopVfx stops them). One-shots whose
		// carrier outlived the window — or whose carrier already went away — are reclaimed.
		if (Pair.Value.bLooping)
		{
			continue;
		}
		const bool bCarrierGone = !Pair.Value.Carrier.IsValid();
		const bool bStale = (Now - Pair.Value.SpawnRealTime) >= ReclaimSeconds;
		if (bCarrierGone || bStale)
		{
			ToReclaim.Add(Pair.Key);
		}
	}
	for (int64 Id : ToReclaim)
	{
		ReleaseTracked(Id);
	}

	// Stop the recurring timer once nothing is tracked, to idle cleanly.
	if (TrackedEffects.Num() == 0)
	{
		if (const UGameInstance* GI = GetGameInstance())
		{
			if (UWorld* World = GI->GetWorld())
			{
				World->GetTimerManager().ClearTimer(SweepTimerHandle);
			}
		}
	}
}

// =====================================================================================================
// Debug
// =====================================================================================================

FString UWS_VfxManagerSubsystem::GetDPDebugString_Implementation() const
{
	int32 Looping = 0;
	for (const TPair<int64, FWS_TrackedVfx>& Pair : TrackedEffects)
	{
		if (Pair.Value.bLooping)
		{
			++Looping;
		}
	}
	return FString::Printf(TEXT("VFX: banks=%d effects=%d tracked=%d (looping=%d) fx=%s"),
		RegisteredBanks.Num(), EntryIndex.Num(), TrackedEffects.Num(), Looping,
		bFxAvailable ? TEXT("on") : TEXT("off"));
}
