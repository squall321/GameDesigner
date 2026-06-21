// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Lobby/ANet_LobbyState.h"
#include "DesignPatternsNetNativeTags.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

ANet_LobbyState::ANet_LobbyState()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	NetUpdateFrequency = 10.f; // lobby state changes are infrequent; a low cadence is ample
}

void ANet_LobbyState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ANet_LobbyState, Roster);
	DOREPLIFETIME(ANet_LobbyState, LobbyPhase);
	DOREPLIFETIME(ANet_LobbyState, HostSlotId);
	DOREPLIFETIME(ANet_LobbyState, bMigratingHost);
}

void ANet_LobbyState::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Wire the fast array's back-pointer on BOTH server and client so per-item callbacks can notify us.
	Roster.OwnerCarrier = this;
}

void ANet_LobbyState::BeginPlay()
{
	Super::BeginPlay();
	RegisterSelfAsService();
}

void ANet_LobbyState::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->UnregisterService(NetNativeTags::Service_Net_Lobby);
	}
	Super::EndPlay(EndPlayReason);
}

void ANet_LobbyState::RegisterSelfAsService()
{
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		Locator->RegisterService(NetNativeTags::Service_Net_Lobby, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
}

// ---- ISeam_LobbyRead -------------------------------------------------------------------------------

void ANet_LobbyState::GetLobbyMembers_Implementation(TArray<FSeam_LobbyMember>& OutMembers) const
{
	OutMembers.Reset(Roster.Players.Num());
	for (const FNet_LobbyPlayerItem& Item : Roster.Players)
	{
		OutMembers.Add(Item.ToSeamMember());
	}
}

int32 ANet_LobbyState::GetOccupiedSlotCount_Implementation() const
{
	int32 Count = 0;
	for (const FNet_LobbyPlayerItem& Item : Roster.Players)
	{
		if (Item.State != ESeam_LobbyMemberState::Empty)
		{
			++Count;
		}
	}
	return Count;
}

bool ANet_LobbyState::AreAllPlayersReady_Implementation() const
{
	int32 Occupied = 0;
	for (const FNet_LobbyPlayerItem& Item : Roster.Players)
	{
		if (Item.State == ESeam_LobbyMemberState::Empty)
		{
			continue;
		}
		++Occupied;
		const bool bReady = (Item.State == ESeam_LobbyMemberState::Ready || Item.State == ESeam_LobbyMemberState::Host);
		if (!bReady)
		{
			return false;
		}
	}
	return Occupied >= FMath::Max(1, MinPlayersToStart);
}

int32 ANet_LobbyState::GetHostSlotId_Implementation() const
{
	return HostSlotId;
}

bool ANet_LobbyState::IsMigratingHost_Implementation() const
{
	return bMigratingHost;
}

FGameplayTag ANet_LobbyState::GetLobbyPhase_Implementation() const
{
	return LobbyPhase;
}

// ---- Reads ----------------------------------------------------------------------------------------

const FNet_LobbyPlayerItem* ANet_LobbyState::FindByNetId(const FString& PlayerNetId) const
{
	return Roster.Players.FindByPredicate([&PlayerNetId](const FNet_LobbyPlayerItem& I){ return I.PlayerNetId == PlayerNetId; });
}

const FNet_LobbyPlayerItem* ANet_LobbyState::FindBySlot(int32 SlotId) const
{
	return Roster.Players.FindByPredicate([SlotId](const FNet_LobbyPlayerItem& I){ return I.SlotId == SlotId; });
}

FNet_LobbyPlayerItem* ANet_LobbyState::FindByNetIdMutable(const FString& PlayerNetId)
{
	return Roster.Players.FindByPredicate([&PlayerNetId](const FNet_LobbyPlayerItem& I){ return I.PlayerNetId == PlayerNetId; });
}

FNet_LobbyPlayerItem* ANet_LobbyState::FindBySlotMutable(int32 SlotId)
{
	return Roster.Players.FindByPredicate([SlotId](const FNet_LobbyPlayerItem& I){ return I.SlotId == SlotId; });
}

int32 ANet_LobbyState::AllocateSlotId() const
{
	for (int32 Candidate = 0; Candidate < FMath::Max(1, MaxSlots); ++Candidate)
	{
		const bool bTaken = Roster.Players.ContainsByPredicate([Candidate](const FNet_LobbyPlayerItem& I){ return I.SlotId == Candidate; });
		if (!bTaken)
		{
			return Candidate;
		}
	}
	return INDEX_NONE;
}

// ---- Authority mutators ---------------------------------------------------------------------------

int32 ANet_LobbyState::AddOrRebindPlayer(const FString& PlayerNetId, const FString& PlayerName, bool bIsHost)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_LobbyState::AddOrRebindPlayer")))
	{
		return INDEX_NONE;
	}

	// Reconnect path: re-bind an existing slot (e.g. after a host-migration drop).
	if (FNet_LobbyPlayerItem* Existing = FindByNetIdMutable(PlayerNetId))
	{
		Existing->PlayerName = PlayerName;
		Existing->State = bIsHost ? ESeam_LobbyMemberState::Host : ESeam_LobbyMemberState::Joined;
		if (bIsHost) { HostSlotId = Existing->SlotId; }
		Roster.MarkItemDirty(*Existing);
		NotifyRosterChanged();
		return Existing->SlotId;
	}

	const int32 SlotId = AllocateSlotId();
	if (SlotId == INDEX_NONE)
	{
		UE_LOG(LogDP, Warning, TEXT("Lobby full (%d slots); rejected %s."), MaxSlots, *PlayerName);
		return INDEX_NONE;
	}

	FNet_LobbyPlayerItem Item;
	Item.SlotId = SlotId;
	Item.PlayerNetId = PlayerNetId;
	Item.PlayerName = PlayerName;
	Item.State = bIsHost ? ESeam_LobbyMemberState::Host : ESeam_LobbyMemberState::Joined;
	Roster.Players.Add(Item);
	Roster.MarkItemDirty(Roster.Players.Last());

	if (bIsHost)
	{
		HostSlotId = SlotId;
	}

	NotifyRosterChanged();
	return SlotId;
}

bool ANet_LobbyState::RemovePlayer(const FString& PlayerNetId)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_LobbyState::RemovePlayer")))
	{
		return false;
	}
	const int32 Removed = Roster.Players.RemoveAll([&PlayerNetId](const FNet_LobbyPlayerItem& I){ return I.PlayerNetId == PlayerNetId; });
	if (Removed > 0)
	{
		Roster.MarkArrayDirty();
		NotifyRosterChanged();
		return true;
	}
	return false;
}

bool ANet_LobbyState::MarkReconnecting(const FString& PlayerNetId)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_LobbyState::MarkReconnecting")))
	{
		return false;
	}
	if (FNet_LobbyPlayerItem* Item = FindByNetIdMutable(PlayerNetId))
	{
		Item->State = ESeam_LobbyMemberState::Reconnecting;
		Roster.MarkItemDirty(*Item);
		NotifyRosterChanged();
		return true;
	}
	return false;
}

bool ANet_LobbyState::SetReady(int32 SlotId, bool bReady)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_LobbyState::SetReady")))
	{
		return false;
	}
	FNet_LobbyPlayerItem* Item = FindBySlotMutable(SlotId);
	if (!Item || Item->State == ESeam_LobbyMemberState::Empty || Item->State == ESeam_LobbyMemberState::Host)
	{
		return false; // host is implicitly ready; empty slots cannot ready
	}
	const ESeam_LobbyMemberState NewState = bReady ? ESeam_LobbyMemberState::Ready : ESeam_LobbyMemberState::Joined;
	if (Item->State != NewState)
	{
		Item->State = NewState;
		Roster.MarkItemDirty(*Item);
		NotifyRosterChanged();

		// If everyone is now ready, raise the dedicated bus channel for the match-start flow.
		if (Execute_AreAllPlayersReady(this))
		{
			if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
			{
				Bus->BroadcastPayload(NetNativeTags::Bus_Net_Lobby_AllReady, FInstancedStruct(), this);
			}
		}
	}
	return true;
}

bool ANet_LobbyState::AssignTeam(int32 SlotId, FGameplayTag TeamTag)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_LobbyState::AssignTeam")))
	{
		return false;
	}
	if (FNet_LobbyPlayerItem* Item = FindBySlotMutable(SlotId))
	{
		Item->TeamTag = TeamTag;
		Roster.MarkItemDirty(*Item);
		NotifyRosterChanged();
		return true;
	}
	return false;
}

bool ANet_LobbyState::SetParty(int32 SlotId, FGameplayTag PartyTag)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_LobbyState::SetParty")))
	{
		return false;
	}
	if (FNet_LobbyPlayerItem* Item = FindBySlotMutable(SlotId))
	{
		Item->PartyTag = PartyTag;
		Roster.MarkItemDirty(*Item);
		NotifyRosterChanged();
		return true;
	}
	return false;
}

void ANet_LobbyState::SetLobbyPhase(FGameplayTag PhaseTag)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_LobbyState::SetLobbyPhase")))
	{
		return;
	}
	if (LobbyPhase != PhaseTag)
	{
		LobbyPhase = PhaseTag;
		WakeForChange();
		OnRep_ScalarState(); // mirror server-side
	}
}

void ANet_LobbyState::SetMigratingHost(bool bMigrating)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(this, TEXT("ANet_LobbyState::SetMigratingHost")))
	{
		return;
	}
	if (bMigratingHost != bMigrating)
	{
		bMigratingHost = bMigrating;
		WakeForChange();
		OnRep_ScalarState();

		if (bMigrating)
		{
			if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
			{
				Bus->BroadcastPayload(NetNativeTags::Bus_Net_HostMigration, FInstancedStruct(), this);
			}
		}
	}
}

// ---- Change plumbing ------------------------------------------------------------------------------

void ANet_LobbyState::NotifyRosterChanged()
{
	WakeForChange();
	OnLobbyChanged.Broadcast();

	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(NetNativeTags::Bus_Net_Lobby_Changed, FInstancedStruct(), this);
	}
}

void ANet_LobbyState::HandleReplicatedRosterChange()
{
	// Client side: a fast-array item callback fired. Surface the change to UI listeners.
	OnLobbyChanged.Broadcast();
}

void ANet_LobbyState::OnRep_ScalarState()
{
	OnLobbyChanged.Broadcast();
}

void ANet_LobbyState::WakeForChange()
{
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
	}
	ForceNetUpdate();
}
