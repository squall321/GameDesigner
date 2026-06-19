// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Action/DPGameplayActionLite.h"
#include "Action/DPGameplayActionComponent.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

DECLARE_CYCLE_STAT(TEXT("Action Activate"), STAT_DP_ActionActivate, STATGROUP_DesignPatterns);

UDP_GameplayActionLite::UDP_GameplayActionLite()
{
}

UWorld* UDP_GameplayActionLite::GetWorld() const
{
	// CDOs and the transient package have no world; guard so this is safe for BP function calls.
	if (HasAllFlags(RF_ClassDefaultObject) || GetOuter() == nullptr)
	{
		return nullptr;
	}
	if (const UObject* Outer = GetOuter())
	{
		return Outer->GetWorld();
	}
	return nullptr;
}

UDP_GameplayActionComponent* UDP_GameplayActionLite::GetOwningComponent() const
{
	// The granting component is this action's Outer (NewObject(Component, ...)).
	return Cast<UDP_GameplayActionComponent>(GetOuter());
}

bool UDP_GameplayActionLite::CanActivate_Implementation(const FDP_ActionActivationData& Data) const
{
	const UDP_GameplayActionComponent* Component = Data.SourceComponent.IsValid()
		? Data.SourceComponent.Get()
		: GetOwningComponent();

	if (!Component)
	{
		// No component context (e.g. CDO query) — default to allowed; the component re-checks.
		return true;
	}

	const FGameplayTagContainer& OwnerTags = Component->GetOwnedTags();

	if (!ActivationRequiredTags.IsEmpty() && !OwnerTags.HasAll(ActivationRequiredTags))
	{
		UE_LOG(LogDPAction, Verbose, TEXT("CanActivate '%s' blocked: missing required tags."), *ActionTag.ToString());
		return false;
	}
	if (!ActivationBlockedTags.IsEmpty() && OwnerTags.HasAny(ActivationBlockedTags))
	{
		UE_LOG(LogDPAction, Verbose, TEXT("CanActivate '%s' blocked: owner has a blocking tag."), *ActionTag.ToString());
		return false;
	}
	return true;
}

bool UDP_GameplayActionLite::Activate_Implementation(const FDP_ActionActivationData& Data)
{
	SCOPE_CYCLE_COUNTER(STAT_DP_ActionActivate);
	UE_LOG(LogDPAction, Verbose, TEXT("Action '%s' activated (instigator '%s')."),
		*ActionTag.ToString(),
		Data.Instigator.IsValid() ? *Data.Instigator->GetName() : TEXT("<none>"));
	// Base implementation: succeed so cooldown applies. Subclasses do real work and may return false.
	return true;
}

void UDP_GameplayActionLite::EndAction_Implementation(const FDP_ActionActivationData& /*Data*/, bool bWasCancelled)
{
	UE_LOG(LogDPAction, Verbose, TEXT("Action '%s' ended (%s)."),
		*ActionTag.ToString(), bWasCancelled ? TEXT("cancelled") : TEXT("completed"));
}
