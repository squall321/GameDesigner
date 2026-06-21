// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Logic/Narr_Effect_AdjustReputation.h"
#include "Reputation/Narr_ReputationSubsystem.h"   // concrete owner (same module — fine to include)
#include "Story/Narr_StoryNativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

void UNarr_Effect_AdjustReputation::Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	UObject* WorldContext = Source.GetObject();
	if (!WorldContext || !FactionOrNpcTag.IsValid() || Delta == 0.f)
	{
		return;
	}

	// Resolve the reputation owner. We need the concrete subsystem (the Add* mutators are not on the read
	// seam); Narrative owns both, so a concrete resolution within the module is allowed.
	UNarr_ReputationSubsystem* Rep = nullptr;
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContext))
	{
		Rep = Cast<UNarr_ReputationSubsystem>(
			Locator->ResolveService(NarrativeStoryNativeTags::Service_Narrative_Reputation));
	}
	if (!Rep)
	{
		// Fall back to the GameInstance subsystem directly.
		Rep = FDP_SubsystemStatics::GetGameInstanceSubsystem<UNarr_ReputationSubsystem>(WorldContext);
	}
	if (!Rep)
	{
		UE_LOG(LogDP, Verbose, TEXT("UNarr_Effect_AdjustReputation: no reputation subsystem; no-op."));
		return;
	}

	// The Add* mutators guard authority at the TOP, so this is a safe no-op on clients.
	if (bIsNpcAffinity)
	{
		Rep->AddNpcAffinity(FactionOrNpcTag, Delta);
	}
	else
	{
		Rep->AddFactionReputation(FactionOrNpcTag, Delta);
	}
}

FString UNarr_Effect_AdjustReputation::DescribeEffect() const
{
	return FString::Printf(TEXT("AdjustReputation(%s %+0.0f%s)"),
		*FactionOrNpcTag.ToString(), Delta, bIsNpcAffinity ? TEXT(" npc") : TEXT(""));
}
