// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Killcam/Rep_KillcamComponent.h"
#include "DesignPatternsReplayModule.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPLog.h"

#include "Net/UnrealNetwork.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

URep_KillcamComponent::URep_KillcamComponent()
{
	// Killcam state is replicated (the only replicated state in the module), but the component never ticks.
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}

void URep_KillcamComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Per-player cosmetic: replicate the death record to the owning connection only.
	DOREPLIFETIME_CONDITION(URep_KillcamComponent, LastDeath, COND_OwnerOnly);
}

void URep_KillcamComponent::ReportDeath(const FRep_KillcamRecord& Record)
{
	// AUTHORITY GUARD AT THE TOP: only the server mutates the authoritative, replicated death record.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		UE_LOG(LogDP, Verbose, TEXT("Killcam: ReportDeath ignored on non-authority."));
		return;
	}

	if (!Record.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("Killcam: ReportDeath given an invalid record (no victim)."));
		return;
	}

	LastDeath = Record;

	// On a listen server / standalone there is no OnRep for the local player; notify locally too.
	NotifyDeathLocally();

	UE_LOG(LogDP, Verbose, TEXT("Killcam: recorded death victim=%s killer=%s t=%.2f"),
		*Record.Victim.ToString(), *Record.Killer.ToString(), Record.DeathTimeSeconds);
}

bool URep_KillcamComponent::Server_ReportKillcamMoment_Validate(const FRep_KillcamRecord& Record)
{
	// Reject obviously-malformed intents at the network boundary: a moment must name a victim and carry
	// a non-negative time. (Magnitude is a closed variant; nothing further to validate structurally.)
	return Record.Victim.IsValid() && Record.DeathTimeSeconds >= 0.f;
}

void URep_KillcamComponent::Server_ReportKillcamMoment_Implementation(const FRep_KillcamRecord& Record)
{
	// AUTHORITY GUARD AT THE TOP (a server RPC body runs on the server, but we guard defensively per the
	// house rule that every mutator of replicated state checks HasAuthority first).
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// The server adopts the client's reported moment only if it does not already have a record for this
	// death (the authoritative server record always wins where both exist).
	if (!LastDeath.IsValid())
	{
		LastDeath = Record;
		NotifyDeathLocally();
	}
}

void URep_KillcamComponent::OnRep_LastDeath()
{
	// Client-side: the replicated record arrived; surface it locally to drive the death-cam.
	NotifyDeathLocally();
}

void URep_KillcamComponent::NotifyDeathLocally()
{
	if (!LastDeath.IsValid())
	{
		return;
	}

	OnKillcamDeath.Broadcast(LastDeath);

	// Mirror onto the bus so a local killcam director (or HUD) can react without binding this component.
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UDP_MessageBusSubsystem* Bus = GI->GetSubsystem<UDP_MessageBusSubsystem>())
			{
				Bus->BroadcastPayload(Rep_NativeTags::Bus_Replay_Death, FInstancedStruct(), this);
			}
		}
	}
}
