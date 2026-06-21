// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Net/Seam_LobbyRead.h"

// Fail-closed native defaults for the lobby read seam. A missing/half-built provider reports an
// empty, not-ready lobby so the match-start flow never auto-starts against a phantom roster.

void ISeam_LobbyRead::GetLobbyMembers_Implementation(TArray<FSeam_LobbyMember>& OutMembers) const
{
	OutMembers.Reset();
}

int32 ISeam_LobbyRead::GetOccupiedSlotCount_Implementation() const
{
	return 0;
}

bool ISeam_LobbyRead::AreAllPlayersReady_Implementation() const
{
	return false;
}

int32 ISeam_LobbyRead::GetHostSlotId_Implementation() const
{
	return INDEX_NONE;
}

bool ISeam_LobbyRead::IsMigratingHost_Implementation() const
{
	return false;
}

FGameplayTag ISeam_LobbyRead::GetLobbyPhase_Implementation() const
{
	return FGameplayTag();
}
