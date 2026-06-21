// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Spectator/UNet_SpectatorComponent.h"
#include "Identity/Seam_TeamAffinity.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

UNet_SpectatorComponent::UNet_SpectatorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UNet_SpectatorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Only the owning client needs to know who it is spectating.
	DOREPLIFETIME_CONDITION(UNet_SpectatorComponent, SpectateTarget, COND_OwnerOnly);
}

bool UNet_SpectatorComponent::IsTargetAllowed(const AActor* Candidate) const
{
	if (!Candidate || Candidate == GetOwner())
	{
		return false;
	}
	if (SpectatePolicy == ENet_SpectatePolicy::Anyone)
	{
		return true;
	}

	// Resolve the team seam to enforce teammates/enemies-only policy.
	const AActor* Self = GetOwner();
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		static const FGameplayTag TeamServiceTag = FGameplayTag::RequestGameplayTag(TEXT("DP.Service.GameMode.Team"), /*ErrorIfNotFound=*/false);
		if (TeamServiceTag.IsValid())
		{
			if (UObject* Provider = Locator->ResolveService(TeamServiceTag))
			{
				if (Provider->Implements<USeam_TeamAffinity>())
				{
					const bool bFriendly = ISeam_TeamAffinity::Execute_AreFriendly(Provider, Self, Candidate);
					return (SpectatePolicy == ENet_SpectatePolicy::TeammatesOnly) ? bFriendly : !bFriendly;
				}
			}
		}
	}

	// No team seam available: fail closed for restrictive policies (deny rather than leak vision).
	return false;
}

void UNet_SpectatorComponent::GatherCandidates(TArray<AActor*>& OutCandidates) const
{
	OutCandidates.Reset();
	const UWorld* W = GetWorld();
	const AGameStateBase* GS = W ? W->GetGameState() : nullptr;
	if (!GS)
	{
		return;
	}
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (!PS)
		{
			continue;
		}
		if (APawn* Pawn = PS->GetPawn())
		{
			if (IsTargetAllowed(Pawn))
			{
				OutCandidates.Add(Pawn);
			}
		}
	}
}

// ---- Spectate ------------------------------------------------------------------------------------

void UNet_SpectatorComponent::RequestSpectate(AActor* Target)
{
	ServerRequestSpectate(Target);
}

bool UNet_SpectatorComponent::ServerRequestSpectate_Validate(AActor* /*Target*/)
{
	return true; // detailed authorization happens in the impl
}

void UNet_SpectatorComponent::ServerRequestSpectate_Implementation(AActor* Target)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UNet_SpectatorComponent::ServerRequestSpectate")))
	{
		return;
	}
	if (!Target || !IsTargetAllowed(Target))
	{
		UE_LOG(LogDP, Verbose, TEXT("Spectator: rejected target %s under policy %d."),
			Target ? *Target->GetName() : TEXT("<null>"), (int32)SpectatePolicy);
		return;
	}
	SpectateTarget = Target;
	OnRep_SpectateTarget(); // mirror server-side (listen host owns a view)
}

void UNet_SpectatorComponent::RequestCycleTarget(bool bForward)
{
	ServerRequestCycleTarget(bForward);
}

bool UNet_SpectatorComponent::ServerRequestCycleTarget_Validate(bool /*bForward*/)
{
	return true;
}

void UNet_SpectatorComponent::ServerRequestCycleTarget_Implementation(bool bForward)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UNet_SpectatorComponent::ServerRequestCycleTarget")))
	{
		return;
	}
	TArray<AActor*> Candidates;
	GatherCandidates(Candidates);
	if (Candidates.Num() == 0)
	{
		return;
	}

	int32 CurrentIndex = Candidates.IndexOfByKey(SpectateTarget.Get());
	if (CurrentIndex == INDEX_NONE)
	{
		CurrentIndex = bForward ? -1 : 0;
	}
	const int32 Next = (CurrentIndex + (bForward ? 1 : -1) + Candidates.Num()) % Candidates.Num();
	SpectateTarget = Candidates[Next];
	OnRep_SpectateTarget();
}

void UNet_SpectatorComponent::RequestStopSpectating()
{
	ServerRequestStopSpectating();
}

bool UNet_SpectatorComponent::ServerRequestStopSpectating_Validate()
{
	return true;
}

void UNet_SpectatorComponent::ServerRequestStopSpectating_Implementation()
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UNet_SpectatorComponent::ServerRequestStopSpectating")))
	{
		return;
	}
	SpectateTarget = nullptr;
	OnRep_SpectateTarget();
}

void UNet_SpectatorComponent::OnRep_SpectateTarget()
{
	AActor* Target = SpectateTarget.Get();
	OnSpectatorTargetChanged.Broadcast(Target);

	// On the owning client, point the controller's view at the confirmed target (or back to self).
	if (APlayerController* PC = Cast<APlayerController>(GetOwner()))
	{
		if (PC->IsLocalController())
		{
			AActor* ViewTarget = Target ? Target : Cast<AActor>(PC->GetPawn());
			if (ViewTarget)
			{
				PC->SetViewTargetWithBlend(ViewTarget, FMath::Max(0.f, ViewBlendTime));
			}
		}
	}
}
