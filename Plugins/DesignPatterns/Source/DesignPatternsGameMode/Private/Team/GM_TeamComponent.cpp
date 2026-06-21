// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Team/GM_TeamComponent.h"

#include "Core/DPLog.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UGM_TeamComponent::UGM_TeamComponent()
{
	// Pure state carrier: no tick needed; replicate so clients can color/target/gate on team.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UGM_TeamComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Only the team tag replicates — the minimal must-be-synced surface.
	DOREPLIFETIME(UGM_TeamComponent, TeamTag);
}

void UGM_TeamComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const AActor* Owner = GetOwner())
	{
		UE_LOG(LogDP, Verbose, TEXT("GM_TeamComponent: BeginPlay on %s (team=%s)."),
			*Owner->GetName(), *TeamTag.ToString());
	}
}

bool UGM_TeamComponent::SetTeamTag(FGameplayTag NewTeamTag)
{
	// HARD RULE 5: every mutator of replicated state guards authority at the TOP and early-returns on clients.
	const AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		UE_LOG(LogDP, Verbose,
			TEXT("GM_TeamComponent::SetTeamTag rejected on non-authority for %s."),
			Owner ? *Owner->GetName() : TEXT("<null owner>"));
		return false;
	}

	if (TeamTag == NewTeamTag)
	{
		return false;
	}

	const FGameplayTag PreviousTeam = TeamTag;
	TeamTag = NewTeamTag;

	// RepNotify does not fire on the authority — raise the local notification explicitly so server-side
	// listeners (and listen-server clients) see the same OnTeamChanged event as remote clients.
	OnTeamChanged.Broadcast(this, PreviousTeam, TeamTag);

	UE_LOG(LogDP, Log, TEXT("GM_TeamComponent: %s team %s -> %s (authority)."),
		Owner ? *Owner->GetName() : TEXT("<actor>"), *PreviousTeam.ToString(), *TeamTag.ToString());
	return true;
}

void UGM_TeamComponent::OnRep_TeamTag(FGameplayTag PreviousTeam)
{
	// Client (or simulated proxy) learned the new team. Mirror the authority's notification.
	UE_LOG(LogDP, Verbose, TEXT("GM_TeamComponent::OnRep_TeamTag %s -> %s on %s."),
		*PreviousTeam.ToString(), *TeamTag.ToString(), GetOwner() ? *GetOwner()->GetName() : TEXT("<actor>"));
	OnTeamChanged.Broadcast(this, PreviousTeam, TeamTag);
}

UGM_TeamComponent* UGM_TeamComponent::FindOn(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UGM_TeamComponent>() : nullptr;
}
