// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Lobby/FNet_LobbyTypes.h"
#include "Lobby/ANet_LobbyState.h"

void FNet_LobbyPlayerItem::PreReplicatedRemove(const FNet_LobbyPlayerArray& InArraySerializer)
{
	if (ANet_LobbyState* Carrier = InArraySerializer.OwnerCarrier)
	{
		Carrier->HandleReplicatedRosterChange();
	}
}

void FNet_LobbyPlayerItem::PostReplicatedAdd(const FNet_LobbyPlayerArray& InArraySerializer)
{
	if (ANet_LobbyState* Carrier = InArraySerializer.OwnerCarrier)
	{
		Carrier->HandleReplicatedRosterChange();
	}
}

void FNet_LobbyPlayerItem::PostReplicatedChange(const FNet_LobbyPlayerArray& InArraySerializer)
{
	if (ANet_LobbyState* Carrier = InArraySerializer.OwnerCarrier)
	{
		Carrier->HandleReplicatedRosterChange();
	}
}
