// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Spawn/AI_SpawnDirectorSubsystem.h"
#include "Spawn/AI_EncounterDataAsset.h"
#include "Spawn/AI_WaveDataAsset.h"
#include "Settings/AI_DeveloperSettings.h"
#include "Seams/AI_SpawnParticipant.h"
#include "Seams/AI_SpawnRegionProvider.h"
#include "DesignPatternsAINativeTags.h"
#include "AI_BusPayloads.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Factory/DPSpawnFactorySubsystem.h"
#include "Factory/DPSpawnRecipe.h"

// Seams (clock) and the World hub are PRIVATE deps — resolved here in the .cpp only.
#include "Clock/Seam_SimClock.h"
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Query/WorldHub_Queryable.h"
#include "Hub/WorldHub_Scope.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject on 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

namespace
{
	/** Defensive fallbacks used only if the settings CDO is somehow null (never in a running game). */
	constexpr int32 GDefaultBudget_Fallback = 20;
	constexpr int32 GSpawnCap_Fallback = 100;
	constexpr float GDirectorTickHz_Fallback = 5.f;
	constexpr int32 GMaxSpawnsPerTick_Fallback = 8;
	constexpr float GFallbackRegionRadius_Fallback = 300.f;
}

void UAI_SpawnDirectorSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	RegisterSelfAsService();

	// Register the pacing ticker. The subsystem is intentionally NOT an FTickableGameObject so it does
	// not tick in editor / during seamless travel; the ticker drains while the world is live.
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAI_SpawnDirectorSubsystem::TickDirector));
}

void UAI_SpawnDirectorSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		if (Locator->ResolveService(AINativeTags::Service_AI_SpawnDirector) == this)
		{
			Locator->UnregisterService(AINativeTags::Service_AI_SpawnDirector);
		}
	}
	ResetEncounterState();
	Super::Deinitialize();
}

UDP_ServiceLocatorSubsystem* UAI_SpawnDirectorSubsystem::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

UDP_SpawnFactorySubsystem* UAI_SpawnDirectorSubsystem::GetFactory() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UDP_SpawnFactorySubsystem>(this);
}

void UAI_SpawnDirectorSubsystem::RegisterSelfAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		Locator->RegisterService(AINativeTags::Service_AI_SpawnDirector, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

//~ Clock ----------------------------------------------------------------------------------------

double UAI_SpawnDirectorSubsystem::GetClockTimeScale() const
{
	// The sim clock is OPTIONAL: with no clock the director paces in real time (scale 1). Whoever owns
	// the authoritative clock registers it under a well-known service key implementing ISeam_SimClock;
	// resolving an unbound key simply returns null, so this is safe and side-effect-free.
	UDP_ServiceLocatorSubsystem* Locator = GetLocator();
	if (!Locator)
	{
		return 1.0;
	}

	// Conventional clock key (the sim-agents clock self-registers under DP.Service.SimAgents.Clock).
	static const FGameplayTag ClockKey =
		FGameplayTag::RequestGameplayTag(FName("DP.Service.SimAgents.Clock"), /*ErrorIfNotFound=*/false);
	if (!ClockKey.IsValid())
	{
		return 1.0;
	}

	UObject* ClockObj = Locator->ResolveService(ClockKey);
	if (!ClockObj || !ClockObj->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
	{
		return 1.0;
	}
	if (ISeam_SimClock::Execute_IsPaused(ClockObj))
	{
		return 0.0;
	}
	return ISeam_SimClock::Execute_GetTimeScale(ClockObj);
}

//~ Encounter control ----------------------------------------------------------------------------

bool UAI_SpawnDirectorSubsystem::ActivateEncounter(UAI_EncounterDataAsset* Encounter, float ProgressionInput)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	if (!Encounter)
	{
		UE_LOG(LogDP, Warning, TEXT("AI SpawnDirector ActivateEncounter rejected: null encounter."));
		return false;
	}
	if (ActiveEncounter)
	{
		UE_LOG(LogDP, Warning, TEXT("AI SpawnDirector: an encounter (%s) is already active; stop it first."),
			*ActiveEncounter->GetEffectiveEncounterTag().ToString());
		return false;
	}

	ResetEncounterState();
	ActiveEncounter = Encounter;
	PendingProgressionInput = ProgressionInput;
	EncounterSimTime = 0.0;

	if (AreGateConditionsMet())
	{
		BeginEncounterRun();
	}
	else
	{
		// Defer: re-check the gate each pacing tick until it passes.
		bPendingGate = true;
		UE_LOG(LogDP, Log, TEXT("AI SpawnDirector: encounter %s pending hub gate conditions."),
			*Encounter->GetEffectiveEncounterTag().ToString());
	}
	return true;
}

bool UAI_SpawnDirectorSubsystem::StopEncounter()
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	if (!ActiveEncounter)
	{
		return false;
	}
	UE_LOG(LogDP, Log, TEXT("AI SpawnDirector: stopping encounter %s."),
		*ActiveEncounter->GetEffectiveEncounterTag().ToString());
	ResetEncounterState();
	return true;
}

void UAI_SpawnDirectorSubsystem::ResetEncounterState()
{
	ActiveEncounter = nullptr;
	CurrentWave = nullptr;
	bPendingGate = false;
	Phase = EWavePhase::Idle;
	TotalBudget = 0;
	ConsumedBudget = 0;
	CurrentWaveIndex = 0;
	NextWaveStartSimTime = 0.0;
	ActiveDifficulty = 1.f;
	EntryCursors.Reset();
	Tracked.Reset();
}

//~ Encounter run --------------------------------------------------------------------------------

void UAI_SpawnDirectorSubsystem::BeginEncounterRun()
{
	check(ActiveEncounter);
	bPendingGate = false;

	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const int32 SpawnCap = Settings ? Settings->SpawnCap : GSpawnCap_Fallback;

	// Sample budget and difficulty once from the asset curves against the progression input.
	int32 Budget = ActiveEncounter->SampleBudget(PendingProgressionInput);
	if (Budget <= 0)
	{
		Budget = Settings ? Settings->DefaultEncounterBudget : GDefaultBudget_Fallback;
	}
	TotalBudget = FMath::Clamp(Budget, 1, SpawnCap);
	ActiveDifficulty = ActiveEncounter->SampleDifficulty(PendingProgressionInput);
	ConsumedBudget = 0;
	CurrentWaveIndex = 0;
	Phase = EWavePhase::Idle;
	NextWaveStartSimTime = EncounterSimTime; // first wave's lead-in is applied in StartCurrentWave

	BroadcastEncounterEvent(AINativeTags::Bus_AI_Encounter_Activated);
	UE_LOG(LogDP, Log, TEXT("AI SpawnDirector: encounter %s activated (budget %d, difficulty %.2f, waves %d)."),
		*ActiveEncounter->GetEffectiveEncounterTag().ToString(), TotalBudget, ActiveDifficulty,
		ActiveEncounter->GetWaveCount());
}

bool UAI_SpawnDirectorSubsystem::StartCurrentWave()
{
	check(ActiveEncounter);
	if (!ActiveEncounter->Waves.IsValidIndex(CurrentWaveIndex))
	{
		return false;
	}

	// Resolve (load) the wave soft pointer.
	UAI_WaveDataAsset* Wave = ActiveEncounter->Waves[CurrentWaveIndex].LoadSynchronous();
	if (!Wave)
	{
		UE_LOG(LogDP, Warning, TEXT("AI SpawnDirector: wave slot %d of encounter %s failed to load; skipping."),
			CurrentWaveIndex, *ActiveEncounter->GetEffectiveEncounterTag().ToString());
		return false;
	}

	CurrentWave = Wave;

	// Grant per-wave bonus budget, still clamped to the spawn cap.
	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const int32 SpawnCap = Settings ? Settings->SpawnCap : GSpawnCap_Fallback;
	TotalBudget = FMath::Clamp(TotalBudget + FMath::Max(0, Wave->BonusBudget), 1, SpawnCap);

	// Set up an entry cursor per entry, seeding each entry's start time from its StartDelay.
	EntryCursors.Reset();
	const float PacingScale = ActiveEncounter->PacingTimeScale;
	for (int32 EntryIndex = 0; EntryIndex < Wave->Entries.Num(); ++EntryIndex)
	{
		FEntryCursor Cursor;
		Cursor.EntryIndex = EntryIndex;
		Cursor.Spawned = 0;
		Cursor.NextSpawnSimTime = EncounterSimTime
			+ static_cast<double>(Wave->Entries[EntryIndex].StartDelaySeconds) * PacingScale;
		EntryCursors.Add(Cursor);
	}

	Phase = EWavePhase::Spawning;
	BroadcastWaveEvent(AINativeTags::Bus_AI_Wave_Started, *Wave, CurrentWaveIndex,
		Wave->GetPlannedCount(), /*SpawnedCount=*/0);
	UE_LOG(LogDP, Log, TEXT("AI SpawnDirector: wave %d (%s) started; planned %d."),
		CurrentWaveIndex, *Wave->GetEffectiveWaveTag().ToString(), Wave->GetPlannedCount());
	return true;
}

void UAI_SpawnDirectorSubsystem::TickWaveSpawning()
{
	if (!CurrentWave)
	{
		return;
	}

	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const int32 MaxPerTick = Settings ? Settings->MaxSpawnsPerTick : GMaxSpawnsPerTick_Fallback;
	const float PacingScale = ActiveEncounter ? ActiveEncounter->PacingTimeScale : 1.f;

	int32 SpawnedThisTick = 0;
	bool bAnyRemaining = false;

	for (FEntryCursor& Cursor : EntryCursors)
	{
		if (!CurrentWave->Entries.IsValidIndex(Cursor.EntryIndex))
		{
			continue;
		}
		const FAI_SpawnEntry& Entry = CurrentWave->Entries[Cursor.EntryIndex];

		while (Cursor.Spawned < Entry.Count)
		{
			bAnyRemaining = true;
			if (SpawnedThisTick >= MaxPerTick)
			{
				break; // spread the rest across later ticks
			}
			if (Cursor.NextSpawnSimTime > EncounterSimTime)
			{
				break; // not yet due for this entry
			}
			// Budget gate: only spawn if this instance fits under the live budget AND the spawn cap.
			const int32 Cost = FMath::Max(1, Entry.BudgetCost);
			if (ConsumedBudget + Cost > TotalBudget)
			{
				break; // no budget headroom right now; retry next tick as participants die
			}

			AActor* Spawned = SpawnOneFromEntry(Entry, CurrentWave->GetEffectiveWaveTag());
			if (!Spawned)
			{
				// Spawn failed (bad class / no transform); do not loop forever — treat as consumed slot.
				UE_LOG(LogDP, Warning, TEXT("AI SpawnDirector: entry %d of wave %d failed to spawn; advancing cursor."),
					Cursor.EntryIndex, CurrentWaveIndex);
			}
			++Cursor.Spawned;
			++SpawnedThisTick;
			Cursor.NextSpawnSimTime = EncounterSimTime
				+ static_cast<double>(Entry.PerSpawnIntervalSeconds) * PacingScale;
		}

		if (SpawnedThisTick >= MaxPerTick)
		{
			break;
		}
	}

	if (!bAnyRemaining)
	{
		// Every entry's planned count has been spawned: the wave is COMPLETE (spawning-wise).
		const int32 PlannedCount = CurrentWave->GetPlannedCount();
		BroadcastWaveEvent(AINativeTags::Bus_AI_Wave_Completed, *CurrentWave, CurrentWaveIndex,
			PlannedCount, PlannedCount);

		if (CurrentWave->bRequireClearBeforeNext)
		{
			Phase = EWavePhase::AwaitingClear;
		}
		else
		{
			AdvanceWaveOrComplete();
		}
	}
}

void UAI_SpawnDirectorSubsystem::AdvanceWaveOrComplete()
{
	check(ActiveEncounter);

	const int32 FinishedIndex = CurrentWaveIndex;
	CurrentWave = nullptr;
	EntryCursors.Reset();
	++CurrentWaveIndex;

	if (CurrentWaveIndex >= ActiveEncounter->Waves.Num())
	{
		if (ActiveEncounter->bLooping)
		{
			CurrentWaveIndex = 0; // endless mode: loop back to the first wave
			Phase = EWavePhase::Idle;
			NextWaveStartSimTime = EncounterSimTime;
			UE_LOG(LogDP, Verbose, TEXT("AI SpawnDirector: looping encounter %s back to wave 0."),
				*ActiveEncounter->GetEffectiveEncounterTag().ToString());
			return;
		}

		// Non-looping: the encounter is complete.
		BroadcastEncounterEvent(AINativeTags::Bus_AI_Encounter_Completed);
		UE_LOG(LogDP, Log, TEXT("AI SpawnDirector: encounter %s complete after wave %d."),
			*ActiveEncounter->GetEffectiveEncounterTag().ToString(), FinishedIndex);
		ResetEncounterState();
		return;
	}

	// Schedule the next wave's lead-in (scaled by the encounter pacing scale). The next wave starts once
	// EncounterSimTime reaches this; the wave's own LeadInDelaySeconds is read in StartCurrentWave's gate.
	Phase = EWavePhase::Idle;
	NextWaveStartSimTime = EncounterSimTime;
}

bool UAI_SpawnDirectorSubsystem::IsWaveCleared(const FGameplayTag& WaveTag) const
{
	for (const FTrackedParticipant& T : Tracked)
	{
		if (T.WaveTag == WaveTag)
		{
			return false; // at least one participant of this wave is still tracked (alive)
		}
	}
	return true;
}

//~ Budget reconciliation ------------------------------------------------------------------------

void UAI_SpawnDirectorSubsystem::ReconcileTracked()
{
	// Walk the tracked list, removing dead/destroyed participants and reclaiming their budget. Collect
	// which wave tags lost their last member so we can fire Cleared events.
	TSet<FGameplayTag> WavesThatMightHaveCleared;

	for (int32 Index = Tracked.Num() - 1; Index >= 0; --Index)
	{
		FTrackedParticipant& T = Tracked[Index];

		bool bStillAlive = false;
		if (T.bHasSeam)
		{
			// The seam is a pure C++ interface (not a UFUNCTION); call it directly through the weak ptr.
			if (T.Participant.IsValid())
			{
				IAI_SpawnParticipant* Iface = T.Participant.Get();
				bStillAlive = (Iface != nullptr) && Iface->IsAliveForBudget();
			}
		}
		else
		{
			// No seam: aliveness is "actor still valid and not being destroyed".
			AActor* LiveActor = T.Actor.Get();
			bStillAlive = (LiveActor != nullptr) && IsValid(LiveActor);
		}

		if (!bStillAlive)
		{
			ConsumedBudget = FMath::Max(0, ConsumedBudget - T.BudgetCost);
			WavesThatMightHaveCleared.Add(T.WaveTag);
			Tracked.RemoveAt(Index);
		}
	}

	// For each wave that just lost a member, if it is now fully cleared, fire Wave.Cleared.
	for (const FGameplayTag& WaveTag : WavesThatMightHaveCleared)
	{
		if (WaveTag.IsValid() && IsWaveCleared(WaveTag))
		{
			// We may not have the wave asset loaded any more; broadcast a minimal clear payload.
			if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
			{
				FAI_WaveEventPayload Payload;
				Payload.EncounterTag = ActiveEncounter ? ActiveEncounter->GetEffectiveEncounterTag() : FGameplayTag();
				Payload.WaveTag = WaveTag;
				Payload.RemainingBudget = TotalBudget - ConsumedBudget;
				Bus->BroadcastPayload(AINativeTags::Bus_AI_Wave_Cleared, FInstancedStruct::Make(Payload), this);
			}
			UE_LOG(LogDP, Verbose, TEXT("AI SpawnDirector: wave %s cleared."), *WaveTag.ToString());
		}
	}
}

int32 UAI_SpawnDirectorSubsystem::GetLiveParticipantCount() const
{
	int32 Count = 0;
	for (const FTrackedParticipant& T : Tracked)
	{
		if (T.Participant.IsValid())
		{
			++Count;
		}
	}
	return Count;
}

//~ Spawning -------------------------------------------------------------------------------------

AActor* UAI_SpawnDirectorSubsystem::SpawnOneFromEntry(const FAI_SpawnEntry& Entry, const FGameplayTag& WaveTag)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FTransform SpawnTransform;
	if (!ResolveSpawnTransform(Entry.SpawnRegionTag, SpawnTransform))
	{
		UE_LOG(LogDP, Warning, TEXT("AI SpawnDirector: no spawn transform for region %s; entry skipped."),
			*Entry.SpawnRegionTag.ToString());
		return nullptr;
	}

	AActor* Spawned = nullptr;

	// Preferred path: route through the core factory subsystem (which itself uses the pool when allowed).
	if (Entry.FactoryIdentityTag.IsValid())
	{
		if (UDP_SpawnFactorySubsystem* Factory = GetFactory())
		{
			if (Factory->IsFactoryRegistered(Entry.FactoryIdentityTag))
			{
				FDP_SpawnParams Params;
				Params.IdentityTag = Entry.FactoryIdentityTag;
				Params.Transform = SpawnTransform;
				Params.bAllowPooling = true;
				Params.ContextTags.AddTag(WaveTag);
				Spawned = Factory->Spawn(Entry.FactoryIdentityTag, Params);
			}
		}
	}

	// Fallback path: a direct class spawn from the entry's soft class override.
	if (!Spawned && !Entry.ActorClassOverride.IsNull())
	{
		TSubclassOf<AActor> Class = Entry.ActorClassOverride.LoadSynchronous();
		if (Class)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
			Spawned = World->SpawnActor<AActor>(Class, SpawnTransform, SpawnParams);
		}
	}

	if (!Spawned)
	{
		return nullptr;
	}

	// Register for budget tracking if the spawned actor (or a component) implements the participant seam.
	const int32 Cost = FMath::Max(1, Entry.BudgetCost);
	ConsumedBudget += Cost;

	FTrackedParticipant Tracker;
	Tracker.Actor = Spawned;
	Tracker.BudgetCost = Cost;
	Tracker.WaveTag = WaveTag;
	if (IAI_SpawnParticipant* Participant = Cast<IAI_SpawnParticipant>(Spawned))
	{
		// Preferred: aliveness comes from the seam (handles "dead but not yet destroyed" enemies).
		Tracker.Participant = TWeakInterfacePtr<IAI_SpawnParticipant>(Participant);
		Tracker.bHasSeam = true;
	}
	else
	{
		// No seam: fall back to actor validity for budget reclamation (reclaimed on Destroy).
		Tracker.bHasSeam = false;
	}
	Tracked.Add(Tracker);

	return Spawned;
}

bool UAI_SpawnDirectorSubsystem::ResolveSpawnTransform(const FGameplayTag& RegionTag, FTransform& OutTransform)
{
	++SpawnSeed;

	// 1) Ask a registered ILvl_SpawnRegionProvider first (if any).
	if (UDP_ServiceLocatorSubsystem* Locator = GetLocator())
	{
		if (UObject* ProviderObj = Locator->ResolveService(AINativeTags::Service_AI_SpawnRegions))
		{
			if (ProviderObj->GetClass()->ImplementsInterface(ULvl_SpawnRegionProvider::StaticClass()))
			{
				if (ILvl_SpawnRegionProvider* Provider = Cast<ILvl_SpawnRegionProvider>(ProviderObj))
				{
					if (Provider->HasSpawnRegion(RegionTag)
						&& Provider->GetSpawnTransform(RegionTag, SpawnSeed, OutTransform))
					{
						return true;
					}
				}
			}
		}
	}

	// 2) Fallback to the director's own point list, matched on the region tag.
	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const float Radius = Settings ? Settings->FallbackRegionRadius : GFallbackRegionRadius_Fallback;

	const FAI_FallbackSpawnPoint* Point = FallbackPoints.FindByPredicate(
		[&RegionTag](const FAI_FallbackSpawnPoint& P) { return P.RegionTag == RegionTag; });

	// If the exact region has no point but there is any fallback point at all, use the first as a
	// last resort (documented behaviour) so a mis-tagged wave still spawns rather than silently failing.
	if (!Point && FallbackPoints.Num() > 0)
	{
		Point = &FallbackPoints[0];
	}
	if (!Point)
	{
		return false;
	}

	OutTransform = Point->Transform;
	if (Radius > 0.f)
	{
		// Jitter within a disc on the XY plane around the point so spawns do not stack.
		const FRandomStream Stream(SpawnSeed);
		const float Angle = Stream.FRandRange(0.f, 2.f * PI);
		const float Dist = Radius * FMath::Sqrt(Stream.FRand());
		const FVector Jitter(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
		OutTransform.AddToTranslation(Jitter);
	}
	return true;
}

//~ Gate -----------------------------------------------------------------------------------------

bool UAI_SpawnDirectorSubsystem::AreGateConditionsMet() const
{
	if (!ActiveEncounter || ActiveEncounter->GateConditions.Num() == 0)
	{
		return true; // no gates = always allowed
	}

	// The hub implements IWorldHub_Queryable (the read seam). We use its typed QueryFlag convenience here;
	// other modules would resolve it as a TScriptInterface<IWorldHub_Queryable> from the locator instead.
	UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
	if (!Hub)
	{
		// No hub present: gates referencing hub flags cannot be satisfied. Treat as "not met" so a gated
		// encounter does not fire prematurely without the state spine that backs its conditions.
		UE_LOG(LogDP, Verbose, TEXT("AI SpawnDirector: encounter gated but no World hub present; gate not met."));
		return false;
	}

	for (const FAI_EncounterGateCondition& Cond : ActiveEncounter->GateConditions)
	{
		if (!Cond.HubFlagKey.IsValid())
		{
			continue; // skip malformed conditions (the editor validator already flags these)
		}
		const bool bActual = Hub->QueryFlag(Cond.HubFlagKey, FWorldHub_Scope::Global(), /*bDefault=*/false);
		if (bActual != Cond.bExpectedValue)
		{
			return false; // AND semantics: any failing condition blocks activation
		}
	}
	return true;
}

//~ Ticking --------------------------------------------------------------------------------------

bool UAI_SpawnDirectorSubsystem::TickDirector(float RealDeltaSeconds)
{
	// Only the authority drives spawning; clients never run the director loop.
	if (!HasWorldAuthority() || !ActiveEncounter)
	{
		return true; // keep the ticker registered
	}

	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const float TickHz = FMath::Max(0.5f, Settings ? Settings->DirectorTickHz : GDirectorTickHz_Fallback);
	const float Interval = 1.f / TickHz;

	TickAccumulator += RealDeltaSeconds;
	// Run as many fixed pacing steps as fit (catches up after a hitch without spiralling).
	int32 Guard = 0;
	while (TickAccumulator >= Interval && Guard < 8)
	{
		TickAccumulator -= Interval;
		++Guard;
		StepPacing();
		if (!ActiveEncounter)
		{
			break; // encounter completed during the step
		}
	}
	return true;
}

void UAI_SpawnDirectorSubsystem::StepPacing()
{
	check(ActiveEncounter);

	// Advance the encounter sim clock by the scaled fixed step.
	const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get();
	const float TickHz = FMath::Max(0.5f, Settings ? Settings->DirectorTickHz : GDirectorTickHz_Fallback);
	const double ScaledStep = (1.0 / TickHz) * GetClockTimeScale();
	EncounterSimTime += ScaledStep;

	// Pending gate: keep checking until it passes, then begin the run.
	if (bPendingGate)
	{
		if (AreGateConditionsMet())
		{
			BeginEncounterRun();
		}
		else
		{
			return;
		}
	}

	// Always reconcile budget first so headroom is up to date before we try to spawn.
	ReconcileTracked();

	switch (Phase)
	{
	case EWavePhase::Idle:
	{
		// Apply the next wave's lead-in: wait until sim time reaches the lead-in offset, then start it.
		if (ActiveEncounter->Waves.IsValidIndex(CurrentWaveIndex))
		{
			// Peek the lead-in without loading the asset twice: load, read the delay, gate on it.
			UAI_WaveDataAsset* Wave = ActiveEncounter->Waves[CurrentWaveIndex].LoadSynchronous();
			const float LeadIn = Wave ? Wave->LeadInDelaySeconds : 0.f;
			const double DueAt = NextWaveStartSimTime + static_cast<double>(LeadIn) * ActiveEncounter->PacingTimeScale;
			if (EncounterSimTime >= DueAt)
			{
				if (!StartCurrentWave())
				{
					// The wave failed to load/start: skip to the next (or complete).
					AdvanceWaveOrComplete();
				}
			}
		}
		else
		{
			// No wave at this index (e.g. all done): finalize.
			AdvanceWaveOrComplete();
		}
		break;
	}
	case EWavePhase::Spawning:
		TickWaveSpawning();
		break;
	case EWavePhase::AwaitingClear:
		if (CurrentWave && IsWaveCleared(CurrentWave->GetEffectiveWaveTag()))
		{
			AdvanceWaveOrComplete();
		}
		break;
	default:
		break;
	}
}

//~ Fallback points ------------------------------------------------------------------------------

void UAI_SpawnDirectorSubsystem::RegisterFallbackPoint(const FAI_FallbackSpawnPoint& Point)
{
	if (Point.RegionTag.IsValid())
	{
		FallbackPoints.Add(Point);
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("AI SpawnDirector RegisterFallbackPoint rejected: invalid RegionTag."));
	}
}

int32 UAI_SpawnDirectorSubsystem::ClearFallbackPoints(FGameplayTag RegionTag)
{
	return FallbackPoints.RemoveAll([&RegionTag](const FAI_FallbackSpawnPoint& P) { return P.RegionTag == RegionTag; });
}

//~ Bus helpers ----------------------------------------------------------------------------------

void UAI_SpawnDirectorSubsystem::BroadcastWaveEvent(const FGameplayTag& Channel, const UAI_WaveDataAsset& Wave,
	int32 WaveIndex, int32 PlannedCount, int32 SpawnedCount) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}
	FAI_WaveEventPayload Payload;
	Payload.EncounterTag = ActiveEncounter ? ActiveEncounter->GetEffectiveEncounterTag() : FGameplayTag();
	Payload.WaveTag = Wave.GetEffectiveWaveTag();
	Payload.WaveIndex = WaveIndex;
	Payload.PlannedCount = PlannedCount;
	Payload.SpawnedCount = SpawnedCount;
	Payload.RemainingBudget = TotalBudget - ConsumedBudget;
	Bus->BroadcastPayload(Channel, FInstancedStruct::Make(Payload), const_cast<UAI_SpawnDirectorSubsystem*>(this));
}

void UAI_SpawnDirectorSubsystem::BroadcastEncounterEvent(const FGameplayTag& Channel) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus || !ActiveEncounter)
	{
		return;
	}
	FAI_EncounterEventPayload Payload;
	Payload.EncounterTag = ActiveEncounter->GetEffectiveEncounterTag();
	Payload.WaveCount = ActiveEncounter->GetWaveCount();
	Payload.DifficultyScalar = ActiveDifficulty;
	Bus->BroadcastPayload(Channel, FInstancedStruct::Make(Payload), const_cast<UAI_SpawnDirectorSubsystem*>(this));
}

//~ Debug ----------------------------------------------------------------------------------------

FString UAI_SpawnDirectorSubsystem::GetDPDebugString_Implementation() const
{
	if (!ActiveEncounter)
	{
		return TEXT("AI SpawnDirector: idle");
	}
	const TCHAR* PhaseStr =
		(Phase == EWavePhase::Spawning) ? TEXT("Spawning") :
		(Phase == EWavePhase::AwaitingClear) ? TEXT("AwaitingClear") :
		bPendingGate ? TEXT("PendingGate") : TEXT("Idle");
	return FString::Printf(TEXT("AI SpawnDirector: %s wave %d/%d budget %d/%d live %d"),
		PhaseStr, CurrentWaveIndex, ActiveEncounter->GetWaveCount(),
		ConsumedBudget, TotalBudget, GetLiveParticipantCount());
}
