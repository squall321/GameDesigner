// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Dialogue/Narr_DialogueGraphExtras.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"

FName UNarr_DialogueGraphExtras::GetDataAssetType_Implementation() const
{
	return FName(TEXT("Narr_DialogueExtras"));
}

const FNarr_DialogueNodeExtras* UNarr_DialogueGraphExtras::FindExtras(FGameplayTag NodeId) const
{
	if (!NodeId.IsValid())
	{
		return nullptr;
	}
	return NodeExtras.Find(NodeId);
}

UNarr_DialogueGraphExtras* UNarr_DialogueGraphExtras::ResolveForGraph(const UObject* WorldContext, FGameplayTag GraphDataTag)
{
	if (!WorldContext || !GraphDataTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(WorldContext))
	{
		// Type-filtered resolution: the extras asset shares the graph's DataTag but lives in its own bucket.
		return Registry->Find<UNarr_DialogueGraphExtras>(GraphDataTag);
	}
	return nullptr;
}
