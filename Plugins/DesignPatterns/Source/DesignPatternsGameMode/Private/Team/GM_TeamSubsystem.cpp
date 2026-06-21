// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Team/GM_TeamSubsystem.h"

#include "Team/GM_TeamComponent.h"
#include "Settings/GM_TeamSettings.h"
#include "DesignPatternsGameModeModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/UObjectIterator.h"

// FInstancedStruct (bus payload). Version-gated include — engine moved the header location in 5.5+.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UGM_TeamSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Publish ourselves as the world's ISeam_TeamAffinity provider. WeakObserved: the locator is
	// GameInstance-scoped and survives level travel, so it must NOT hold a hard ref to a world-lifetime
	// subsystem (HARD RULE 3 — cross-world refs are weak/pruned). We unregister in Deinitialize anyway.
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(GameModeNativeTags::Service_GM_TeamAffinity, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
		UE_LOG(LogDPService, Log, TEXT("GM_TeamSubsystem: registered ISeam_TeamAffinity under %s."),
			*GameModeNativeTags::Service_GM_TeamAffinity.GetTag().ToString());
	}
}

void UGM_TeamSubsystem::Deinitialize()
{
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// Only drop the slot if it still points at us (a later world may have overridden it).
		if (Locator->ResolveService(GameModeNativeTags::Service_GM_TeamAffinity) == this)
		{
			Locator->UnregisterService(GameModeNativeTags::Service_GM_TeamAffinity);
		}
	}

	Super::Deinitialize();
}

const UGM_TeamSettings* UGM_TeamSubsystem::GetSettings() const
{
	return UGM_TeamSettings::Get();
}

FGameplayTag UGM_TeamSubsystem::GetTeamTag_Implementation(const AActor* Actor) const
{
	if (const UGM_TeamComponent* Team = UGM_TeamComponent::FindOn(Actor))
	{
		return Team->GetTeamTag();
	}
	return FGameplayTag();
}

EGM_TeamPolicy UGM_TeamSubsystem::GetTeamPolicy() const
{
	const UGM_TeamSettings* Settings = GetSettings();
	// Defensive fallback: classic team match if settings are somehow unavailable.
	return Settings ? Settings->TeamPolicy : EGM_TeamPolicy::TeamMatch;
}

EGM_FriendlyFirePolicy UGM_TeamSubsystem::GetFriendlyFirePolicy() const
{
	const UGM_TeamSettings* Settings = GetSettings();
	// Defensive fallback: no friendly fire (the safe default).
	return Settings ? Settings->FriendlyFirePolicy : EGM_FriendlyFirePolicy::Disabled;
}

float UGM_TeamSubsystem::GetFriendlyFireScalar() const
{
	switch (GetFriendlyFirePolicy())
	{
	case EGM_FriendlyFirePolicy::Enabled:
		return 1.0f;
	case EGM_FriendlyFirePolicy::Reduced:
	{
		const UGM_TeamSettings* Settings = GetSettings();
		return Settings ? Settings->FriendlyFireScalar : 0.0f;
	}
	case EGM_FriendlyFirePolicy::Disabled:
	default:
		return 0.0f;
	}
}

bool UGM_TeamSubsystem::AreTeamsExplicitlyAllied(const FGameplayTag& A, const FGameplayTag& B) const
{
	const UGM_TeamSettings* Settings = GetSettings();
	if (!Settings)
	{
		return false;
	}

	for (const FGM_TeamAllianceRow& Row : Settings->Alliances)
	{
		// Alliances are symmetric.
		if ((Row.TeamA == A && Row.TeamB == B) || (Row.TeamA == B && Row.TeamB == A))
		{
			return true;
		}
	}
	return false;
}

bool UGM_TeamSubsystem::AreTeamsFriendly(const FGameplayTag& A, const FGameplayTag& B) const
{
	switch (GetTeamPolicy())
	{
	case EGM_TeamPolicy::FreeForAll:
		// Nobody is friendly with anybody (an actor is only friendly with ITSELF, handled by caller).
		return false;

	case EGM_TeamPolicy::TeamMatch:
		// Friendly iff exactly the same (valid) team. Two team-less actors are NOT friendly.
		return A.IsValid() && A == B;

	case EGM_TeamPolicy::AllyFaction:
	{
		if (!A.IsValid() || !B.IsValid())
		{
			return false;
		}
		// Hierarchy relation (faction trees) OR an explicit alliance row.
		if (A == B || A.MatchesTag(B) || B.MatchesTag(A))
		{
			return true;
		}
		return AreTeamsExplicitlyAllied(A, B);
	}

	default:
		return false;
	}
}

bool UGM_TeamSubsystem::AreFriendly_Implementation(const AActor* A, const AActor* B) const
{
	if (!A || !B)
	{
		return false;
	}
	// An actor is always friendly with itself, regardless of policy (covers free-for-all self-damage gating).
	if (A == B)
	{
		return true;
	}

	const FGameplayTag TeamA = GetTeamTag_Implementation(A);
	const FGameplayTag TeamB = GetTeamTag_Implementation(B);
	return AreTeamsFriendly(TeamA, TeamB);
}

int32 UGM_TeamSubsystem::GetTeamPopulation(FGameplayTag TeamTag) const
{
	const UWorld* World = GetWorld();
	if (!World || !TeamTag.IsValid())
	{
		return 0;
	}

	int32 Count = 0;
	// Count team components whose owner is on this team. Iterating components is cheap at match scale and
	// avoids the subsystem holding a (leak-prone) registry of actors.
	for (TObjectIterator<UGM_TeamComponent> It; It; ++It)
	{
		const UGM_TeamComponent* Comp = *It;
		if (Comp && Comp->GetWorld() == World && Comp->GetTeamTag() == TeamTag)
		{
			++Count;
		}
	}
	return Count;
}

bool UGM_TeamSubsystem::AssignTeam(AActor* Actor, FGameplayTag TeamTag)
{
	// HARD RULE 5: authority guard at the TOP.
	if (!HasWorldAuthority())
	{
		UE_LOG(LogDP, Verbose, TEXT("GM_TeamSubsystem::AssignTeam rejected on client."));
		return false;
	}
	if (!Actor)
	{
		return false;
	}

	UGM_TeamComponent* Team = UGM_TeamComponent::FindOn(Actor);
	if (!Team)
	{
		UE_LOG(LogDP, Warning, TEXT("GM_TeamSubsystem::AssignTeam: %s has no UGM_TeamComponent."),
			*Actor->GetName());
		return false;
	}

	const FGameplayTag PreviousTeam = Team->GetTeamTag();
	if (!Team->SetTeamTag(TeamTag))
	{
		return false; // No change.
	}

	BroadcastTeamChanged(Actor, PreviousTeam, TeamTag);
	return true;
}

FGameplayTag UGM_TeamSubsystem::AutoAssignBalancedTeam(AActor* Actor)
{
	if (!HasWorldAuthority() || !Actor)
	{
		return FGameplayTag();
	}

	const UGM_TeamSettings* Settings = GetSettings();
	if (!Settings || Settings->AssignableTeams.Num() == 0)
	{
		UE_LOG(LogDP, Verbose,
			TEXT("GM_TeamSubsystem::AutoAssignBalancedTeam: no assignable-team roster configured."));
		return FGameplayTag();
	}

	// Pick the least-populated team; ties resolve to the first in roster order (deterministic on the server).
	FGameplayTag BestTeam;
	int32 BestPopulation = MAX_int32;
	for (const FGameplayTag& Candidate : Settings->AssignableTeams)
	{
		if (!Candidate.IsValid())
		{
			continue;
		}
		const int32 Population = GetTeamPopulation(Candidate);
		if (Population < BestPopulation)
		{
			BestPopulation = Population;
			BestTeam = Candidate;
		}
	}

	if (!BestTeam.IsValid())
	{
		return FGameplayTag();
	}

	// Assign (no-op if already on BestTeam). Either way the actor ends up on BestTeam, so report it.
	AssignTeam(Actor, BestTeam);
	return BestTeam;
}

void UGM_TeamSubsystem::BroadcastTeamChanged(AActor* Actor, FGameplayTag PreviousTeam, FGameplayTag NewTeam) const
{
	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FGM_TeamChangedPayload Payload;
	Payload.PreviousTeam = PreviousTeam;
	Payload.NewTeam = NewTeam;

	const FInstancedStruct Wrapped = FInstancedStruct::Make(Payload);
	Bus->BroadcastPayload(GameModeNativeTags::Bus_GM_TeamChanged, Wrapped, Actor);
}

FString UGM_TeamSubsystem::GetDPDebugString_Implementation() const
{
	const UGM_TeamSettings* Settings = GetSettings();
	const int32 RosterSize = Settings ? Settings->AssignableTeams.Num() : 0;
	return FString::Printf(TEXT("Team: policy=%d ff=%d roster=%d auth=%s"),
		(int32)GetTeamPolicy(), (int32)GetFriendlyFirePolicy(), RosterSize,
		HasWorldAuthority() ? TEXT("Y") : TEXT("N"));
}
