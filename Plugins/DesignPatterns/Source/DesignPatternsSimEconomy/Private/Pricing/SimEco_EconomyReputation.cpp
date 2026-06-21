// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Pricing/SimEco_EconomyReputation.h"
#include "Reputation/Seam_Reputation.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"

ISeam_Reputation* FSimEco_EconomyReputation::Resolve(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}

	// Name-keyed resolution: the economy does not depend on Narrative's tag constants. ErrorIfNotFound
	// is false so a project without the Narrative module simply has no reputation provider.
	static const FGameplayTag RepServiceKey =
		FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Service.Narrative.Reputation")), /*ErrorIfNotFound*/ false);
	if (!RepServiceKey.IsValid())
	{
		return nullptr;
	}

	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContext))
	{
		UObject* Provider = Locator->ResolveService(RepServiceKey);
		// Cast<> recovers the raw ISeam_Reputation interface pointer from the provider UObject (UE's
		// Cast supports native interfaces); returns nullptr if the provider does not implement it.
		return Provider ? Cast<ISeam_Reputation>(Provider) : nullptr;
	}
	return nullptr;
}

bool FSimEco_EconomyReputation::TryGetReputation(const UObject* WorldContext, const AActor* Subject,
	FGameplayTag FactionTag, float& OutValue)
{
	ISeam_Reputation* Provider = Resolve(WorldContext);
	if (!Provider || !Subject || !FactionTag.IsValid())
	{
		return false;
	}
	if (!Provider->HasReputation(Subject))
	{
		// Fail CLOSED: an absent standing is "unknown", not "neutral".
		return false;
	}
	OutValue = Provider->GetReputation(Subject, FactionTag);
	return true;
}

bool FSimEco_EconomyReputation::HasReputation(const UObject* WorldContext, const AActor* Subject)
{
	ISeam_Reputation* Provider = Resolve(WorldContext);
	return Provider && Subject && Provider->HasReputation(Subject);
}
