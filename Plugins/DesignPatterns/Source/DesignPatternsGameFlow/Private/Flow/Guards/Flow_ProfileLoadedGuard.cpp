// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Flow/Guards/Flow_ProfileLoadedGuard.h"
#include "DesignPatternsGameFlowModule.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"

#include "Persist/Seam_SaveSlotManager.h"

bool UFlow_ProfileLoadedGuard::CanTransition_Implementation(FGameplayTag From, FGameplayTag To, FGameplayTag& OutDenyReason) const
{
	// Disabled guard never vetoes.
	if (!bEnabled)
	{
		return true;
	}

	// We only gate entry INTO active gameplay (InGame or a child phase). Every other edge is allowed.
	if (!To.MatchesTag(FlowTags::Phase_InGame))
	{
		return true;
	}

	// Allow re-entry / intra-gameplay transitions (e.g. Pause -> InGame): if we were already in gameplay
	// the profile was necessarily loaded to get there, and a pause overlay must always be exitable.
	if (From.MatchesTag(FlowTags::Phase_InGame) || From == FlowTags::Phase_Pause)
	{
		return true;
	}

	if (IsProfileAvailable())
	{
		return true;
	}

	OutDenyReason = FlowTags::GuardReason_NoProfile;
	UE_LOG(LogDP, Verbose, TEXT("[Flow][Guard] ProfileLoadedGuard vetoed %s -> %s (no profile)."),
		From.IsValid() ? *From.ToString() : TEXT("<none>"), *To.ToString());
	return false;
}

bool UFlow_ProfileLoadedGuard::IsProfileAvailable() const
{
	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		// No locator (very early load): fail-open so the flow is never deadlocked by a missing framework.
		return true;
	}

	UObject* SlotMgr = Locator->ResolveService(FlowTags::Service_SaveSlotManager);
	if (!SlotMgr || !SlotMgr->Implements<USeam_SaveSlotManager>())
	{
		// No save-slot provider registered: the project does not use the save-slot seam to gate gameplay,
		// so we must not block it (fail-open keeps the guard genre-agnostic).
		return true;
	}

	const FString MostRecent = ISeam_SaveSlotManager::Execute_GetMostRecentSlot(SlotMgr);
	return !MostRecent.IsEmpty();
}
