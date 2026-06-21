// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Respawn/GM_RespawnComponent.h"

#include "Team/GM_TeamComponent.h"
#include "Settings/GM_TeamSettings.h"
#include "DesignPatternsGameModeModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"

// The LevelDirector spawn-region seam + its service key (LvlTags::Service_SpawnRegionProvider).
#include "Seam/Lvl_SpawnRegionProvider.h"
#include "DesignPatternsLevelDirectorModule.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"

// FInstancedStruct (bus payload). Version-gated include.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UGM_RespawnComponent::UGM_RespawnComponent()
{
	// Server-side logic only; no tick, no replicated state (HARD RULE 5 — actors carry replicated state).
	PrimaryComponentTick.bCanEverTick = false;
}

void UGM_RespawnComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UGM_RespawnComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

bool UGM_RespawnComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

float UGM_RespawnComponent::ResolveRespawnDelay(float OverrideDelaySeconds) const
{
	if (OverrideDelaySeconds >= 0.0f)
	{
		return OverrideDelaySeconds;
	}
	if (InstanceRespawnDelaySeconds >= 0.0f)
	{
		return InstanceRespawnDelaySeconds;
	}
	const UGM_TeamSettings* Settings = UGM_TeamSettings::Get();
	// Defensive fallback when the CDO is unavailable: a small non-zero delay so we never tight-loop respawn.
	return Settings ? Settings->DefaultRespawnDelaySeconds : 5.0f;
}

FGameplayTag UGM_RespawnComponent::ResolveSpawnFilterTag() const
{
	if (SpawnFilterOverride.IsValid())
	{
		return SpawnFilterOverride;
	}

	// Default: filter by the owning actor's team, IF team-filtered spawning is enabled in settings.
	const UGM_TeamSettings* Settings = UGM_TeamSettings::Get();
	const bool bTeamFiltered = Settings ? Settings->bTeamFilteredSpawning : true;
	if (!bTeamFiltered)
	{
		return FGameplayTag(); // Empty filter = any point the provider offers.
	}

	if (const UGM_TeamComponent* Team = UGM_TeamComponent::FindOn(GetOwner()))
	{
		return Team->GetTeamTag();
	}
	return FGameplayTag();
}

bool UGM_RespawnComponent::IsRespawnPending() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(RespawnTimerHandle);
}

EGM_RespawnResult UGM_RespawnComponent::RequestRespawn(float OverrideDelaySeconds)
{
	// HARD RULE 5: authority guard at the TOP.
	if (!HasAuthority())
	{
		UE_LOG(LogDP, Verbose, TEXT("GM_RespawnComponent::RequestRespawn rejected on non-authority."));
		return EGM_RespawnResult::NoAuthority;
	}

	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!World || !Owner)
	{
		return EGM_RespawnResult::InvalidContext;
	}

	if (IsRespawnPending())
	{
		return EGM_RespawnResult::AlreadyPending;
	}

	// Enforce the per-actor auto-respawn budget (<=0 means unlimited).
	if (const UGM_TeamSettings* Settings = UGM_TeamSettings::Get())
	{
		if (Settings->MaxAutoRespawns > 0 && RespawnCount >= Settings->MaxAutoRespawns)
		{
			UE_LOG(LogDP, Log, TEXT("GM_RespawnComponent: %s exhausted respawn budget (%d)."),
				*Owner->GetName(), Settings->MaxAutoRespawns);
			return EGM_RespawnResult::BudgetExhausted;
		}
	}

	// Re-derive the filter now (server authoritative) so a team change just before death is honored.
	PendingFilterTag = ResolveSpawnFilterTag();

	// Validate that a spawn point even EXISTS before committing to the delay, so callers learn early.
	FTransform Unused;
	if (!SelectSpawnTransform(PendingFilterTag, Unused))
	{
		UE_LOG(LogDP, Warning,
			TEXT("GM_RespawnComponent: no spawn point for %s (filter=%s) — no provider or no match."),
			*Owner->GetName(), *PendingFilterTag.ToString());
		return EGM_RespawnResult::NoSpawnPoint;
	}

	// Announce the queued request (authority) so HUD/UI can show a respawn countdown.
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FGM_RespawnedPayload Pending;
		Pending.TeamTag = PendingFilterTag;
		Pending.RespawnCount = RespawnCount + 1;
		Bus->BroadcastPayload(GameModeNativeTags::Bus_GM_RespawnRequested, FInstancedStruct::Make(Pending), GetOwner());
	}

	const float Delay = ResolveRespawnDelay(OverrideDelaySeconds);
	if (Delay <= 0.0f)
	{
		PerformRespawn();
	}
	else
	{
		FTimerManager& TM = const_cast<UWorld*>(World)->GetTimerManager();
		TM.SetTimer(RespawnTimerHandle, this, &UGM_RespawnComponent::PerformRespawn, Delay, /*bLoop=*/false);
		UE_LOG(LogDP, Log, TEXT("GM_RespawnComponent: %s respawn queued in %.2fs (filter=%s)."),
			*Owner->GetName(), Delay, *PendingFilterTag.ToString());
	}

	return EGM_RespawnResult::Succeeded;
}

void UGM_RespawnComponent::CancelPendingRespawn()
{
	if (!HasAuthority())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RespawnTimerHandle);
	}
}

void UGM_RespawnComponent::PerformRespawn()
{
	// Re-check authority (the timer could in principle outlive an authority change in unusual flows).
	if (!HasAuthority())
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	FTransform SpawnTransform;
	if (!SelectSpawnTransform(PendingFilterTag, SpawnTransform))
	{
		// The provider went away between request and perform; surface it and do nothing.
		UE_LOG(LogDP, Warning,
			TEXT("GM_RespawnComponent: spawn point disappeared before perform for %s."), *Owner->GetName());
		return;
	}

	// Reposition the owner. Movement replication carries the new transform to clients; we sweep=false and
	// teleport so we are not blocked by geometry at the target (the provider is responsible for valid points).
	Owner->SetActorTransform(SpawnTransform, /*bSweep=*/false, /*OutHit=*/nullptr, ETeleportType::ResetPhysics);

	++RespawnCount;

	UE_LOG(LogDP, Log, TEXT("GM_RespawnComponent: %s respawned (#%d) at %s."),
		*Owner->GetName(), RespawnCount, *SpawnTransform.GetLocation().ToCompactString());

	OnRespawned.Broadcast(this, SpawnTransform);

	// Broadcast the authoritative respawned event.
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FGM_RespawnedPayload Payload;
		Payload.TeamTag = PendingFilterTag;
		Payload.SpawnTransform = SpawnTransform;
		Payload.RespawnCount = RespawnCount;
		Bus->BroadcastPayload(GameModeNativeTags::Bus_GM_Respawned, FInstancedStruct::Make(Payload), Owner);
	}
}

bool UGM_RespawnComponent::SelectSpawnTransform(FGameplayTag FilterTag, FTransform& OutTransform) const
{
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return false;
	}

	// Resolve the spawn-region provider seam. Unresolved -> documented inert default (no respawn).
	UObject* ProviderObj = Locator->ResolveService(LvlTags::Service_SpawnRegionProvider);
	if (!ProviderObj || !ProviderObj->Implements<ULvl_SpawnRegionProvider>())
	{
		return false;
	}

	// The seam APPENDS matching transforms; we own the array.
	TArray<FTransform> Candidates;
	ILvl_SpawnRegionProvider::Execute_GetSpawnPoints(ProviderObj, FilterTag, Candidates);

	if (Candidates.Num() == 0)
	{
		return false;
	}

	// Pick a random eligible point so successive respawns spread out. Deterministic enough for gameplay;
	// the provider has already validated each point lies within its region.
	const int32 Index = FMath::RandHelper(Candidates.Num());
	OutTransform = Candidates[Index];
	return true;
}
