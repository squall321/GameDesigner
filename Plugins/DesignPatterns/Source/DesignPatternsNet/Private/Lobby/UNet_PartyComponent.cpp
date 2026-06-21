// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Lobby/UNet_PartyComponent.h"
#include "Lobby/ANet_LobbyState.h"
#include "Lobby/FNet_LobbyTypes.h"
#include "DesignPatternsNetNativeTags.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"

UNet_PartyComponent::UNet_PartyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

ANet_LobbyState* UNet_PartyComponent::ResolveLobby() const
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		return Cast<ANet_LobbyState>(Locator->ResolveService(NetNativeTags::Service_Net_Lobby));
	}
	return nullptr;
}

FString UNet_PartyComponent::GetOwnerNetIdString() const
{
	// Resolve the owning player state through the common owner topologies (PC / Pawn / PlayerState).
	const AActor* Owner = GetOwner();
	const APlayerState* PS = nullptr;
	if (const APlayerController* PC = Cast<APlayerController>(Owner))
	{
		PS = PC->PlayerState;
	}
	else if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		PS = Pawn->GetPlayerState();
	}
	else
	{
		PS = Cast<APlayerState>(Owner);
	}

	if (PS)
	{
		if (const FUniqueNetIdRepl& NetId = PS->GetUniqueId(); NetId.IsValid())
		{
			return NetId->ToString();
		}
		// Fallback: player id is stable within a session even without an online net id.
		return FString::Printf(TEXT("PID:%d"), PS->GetPlayerId());
	}
	return FString();
}

int32 UNet_PartyComponent::ResolveCallerSlotId(ANet_LobbyState* Lobby) const
{
	if (!Lobby)
	{
		return INDEX_NONE;
	}
	const FString NetId = GetOwnerNetIdString();
	if (NetId.IsEmpty())
	{
		return INDEX_NONE;
	}
	if (const FNet_LobbyPlayerItem* Item = Lobby->FindByNetId(NetId))
	{
		return Item->SlotId;
	}
	return INDEX_NONE;
}

// ---- Ready ----------------------------------------------------------------------------------------

void UNet_PartyComponent::RequestReady(bool bReady)
{
	ServerRequestReady(bReady);
}

bool UNet_PartyComponent::ServerRequestReady_Validate(bool /*bReady*/)
{
	return true;
}

void UNet_PartyComponent::ServerRequestReady_Implementation(bool bReady)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UNet_PartyComponent::ServerRequestReady")))
	{
		return;
	}
	ANet_LobbyState* Lobby = ResolveLobby();
	const int32 SlotId = ResolveCallerSlotId(Lobby);
	if (SlotId == INDEX_NONE)
	{
		UE_LOG(LogDP, Verbose, TEXT("PartyComponent: ready request from an unresolved slot; ignored."));
		return;
	}
	Lobby->SetReady(SlotId, bReady);
}

// ---- Team -----------------------------------------------------------------------------------------

void UNet_PartyComponent::RequestJoinTeam(FGameplayTag TeamTag)
{
	ServerRequestJoinTeam(TeamTag);
}

bool UNet_PartyComponent::ServerRequestJoinTeam_Validate(FGameplayTag TeamTag)
{
	// A team request must carry a valid tag; deeper balance policy is enforced in the impl.
	return TeamTag.IsValid();
}

void UNet_PartyComponent::ServerRequestJoinTeam_Implementation(FGameplayTag TeamTag)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UNet_PartyComponent::ServerRequestJoinTeam")))
	{
		return;
	}
	ANet_LobbyState* Lobby = ResolveLobby();
	const int32 SlotId = ResolveCallerSlotId(Lobby);
	if (SlotId == INDEX_NONE)
	{
		return;
	}
	// The carrier's AssignTeam is the authoritative write; team-balance policy lives in the owning flow,
	// which may override this. Here we honour the direct request (the carrier guards authority again).
	Lobby->AssignTeam(SlotId, TeamTag);
}

// ---- Party invite ---------------------------------------------------------------------------------

void UNet_PartyComponent::RequestPartyInvite(int32 TargetSlotId)
{
	ServerRequestPartyInvite(TargetSlotId);
}

bool UNet_PartyComponent::ServerRequestPartyInvite_Validate(int32 TargetSlotId)
{
	return TargetSlotId >= 0;
}

void UNet_PartyComponent::ServerRequestPartyInvite_Implementation(int32 TargetSlotId)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UNet_PartyComponent::ServerRequestPartyInvite")))
	{
		return;
	}
	ANet_LobbyState* Lobby = ResolveLobby();
	const int32 InviterSlot = ResolveCallerSlotId(Lobby);
	if (InviterSlot == INDEX_NONE)
	{
		return;
	}
	const FNet_LobbyPlayerItem* Inviter = Lobby->FindBySlot(InviterSlot);
	const FNet_LobbyPlayerItem* Target = Lobby->FindBySlot(TargetSlotId);
	if (!Inviter || !Target || Target->State == ESeam_LobbyMemberState::Empty)
	{
		return;
	}

	// Derive a party tag from the inviter's slot if they aren't already in a party, then tag the target.
	// (A full accept/decline handshake is project-defined; this simple path assigns directly.)
	FGameplayTag PartyTag = Inviter->PartyTag;
	if (!PartyTag.IsValid())
	{
		// Use the lobby party anchor as a deterministic, valid default. A real project supplies a richer
		// per-party Party.* tag table; this keeps the simple invite path functional and valid.
		PartyTag = NetNativeTags::Net_Lobby_Party;
	}
	Lobby->SetParty(InviterSlot, PartyTag);
	Lobby->SetParty(TargetSlotId, PartyTag);
}

// ---- Leave party ----------------------------------------------------------------------------------

void UNet_PartyComponent::RequestLeaveParty()
{
	ServerRequestLeaveParty();
}

bool UNet_PartyComponent::ServerRequestLeaveParty_Validate()
{
	return true;
}

void UNet_PartyComponent::ServerRequestLeaveParty_Implementation()
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UNet_PartyComponent::ServerRequestLeaveParty")))
	{
		return;
	}
	ANet_LobbyState* Lobby = ResolveLobby();
	const int32 SlotId = ResolveCallerSlotId(Lobby);
	if (SlotId != INDEX_NONE)
	{
		Lobby->SetParty(SlotId, FGameplayTag());
	}
}
