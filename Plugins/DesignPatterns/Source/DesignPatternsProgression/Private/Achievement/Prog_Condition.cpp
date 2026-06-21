// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Achievement/Prog_Condition.h"
#include "Achievement/Prog_AchievementSubsystem.h"
#include "Core/DPSubsystemLibrary.h"

#define LOCTEXT_NAMESPACE "Prog_Condition"

//~ UProg_Condition (base) ---------------------------------------------------------------------

bool UProg_Condition::Evaluate_Implementation(UObject* /*WorldContext*/) const
{
	// Abstract base: an unconfigured condition is never satisfied. Concrete subclasses override.
	return false;
}

FText UProg_Condition::GetConditionDescription_Implementation() const
{
	return LOCTEXT("Generic", "Condition");
}

float UProg_Condition::GetProgressFraction_Implementation(UObject* WorldContext) const
{
	// Default: a boolean condition is either 0% or 100% complete.
	return Evaluate(WorldContext) ? 1.f : 0.f;
}

bool UProg_Condition::ResolveHubFlag(UObject* WorldContext, const FGameplayTag& FlagTag) const
{
	if (!FlagTag.IsValid())
	{
		return false;
	}
	if (const UProg_AchievementSubsystem* Subsystem =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UProg_AchievementSubsystem>(WorldContext))
	{
		return Subsystem->GetHubFlag(FlagTag);
	}
	// Unresolved accumulator (e.g. editor/CDO evaluation): inert default — flag unset.
	return false;
}

int64 UProg_Condition::ResolveHubCounter(UObject* WorldContext, const FGameplayTag& CounterTag) const
{
	if (!CounterTag.IsValid())
	{
		return 0;
	}
	if (const UProg_AchievementSubsystem* Subsystem =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UProg_AchievementSubsystem>(WorldContext))
	{
		return Subsystem->GetHubCounter(CounterTag);
	}
	return 0;
}

//~ UProg_Condition_HubFlag --------------------------------------------------------------------

bool UProg_Condition_HubFlag::Evaluate_Implementation(UObject* WorldContext) const
{
	const bool bSet = ResolveHubFlag(WorldContext, FlagTag);
	return bInvert ? !bSet : bSet;
}

FText UProg_Condition_HubFlag::GetConditionDescription_Implementation() const
{
	return FText::Format(
		bInvert ? LOCTEXT("FlagUnset", "Flag '{0}' is not set") : LOCTEXT("FlagSet", "Flag '{0}' is set"),
		FText::FromString(FlagTag.ToString()));
}

//~ UProg_Condition_HubCounterAtLeast ----------------------------------------------------------

bool UProg_Condition_HubCounterAtLeast::Evaluate_Implementation(UObject* WorldContext) const
{
	return ResolveHubCounter(WorldContext, CounterTag) >= Threshold;
}

float UProg_Condition_HubCounterAtLeast::GetProgressFraction_Implementation(UObject* WorldContext) const
{
	if (Threshold <= 0)
	{
		return 1.f;
	}
	const int64 Current = ResolveHubCounter(WorldContext, CounterTag);
	return FMath::Clamp(static_cast<float>(Current) / static_cast<float>(Threshold), 0.f, 1.f);
}

FText UProg_Condition_HubCounterAtLeast::GetConditionDescription_Implementation() const
{
	return FText::Format(LOCTEXT("CounterAtLeast", "Counter '{0}' >= {1}"),
		FText::FromString(CounterTag.ToString()), FText::AsNumber(Threshold));
}

//~ UProg_Condition_BusCounter -----------------------------------------------------------------

bool UProg_Condition_BusCounter::Evaluate_Implementation(UObject* WorldContext) const
{
	if (const UProg_AchievementSubsystem* Subsystem =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UProg_AchievementSubsystem>(WorldContext))
	{
		return Subsystem->GetChannelHitCount(ChannelTag) >= Threshold;
	}
	return false;
}

float UProg_Condition_BusCounter::GetProgressFraction_Implementation(UObject* WorldContext) const
{
	if (Threshold <= 0)
	{
		return 1.f;
	}
	int64 Current = 0;
	if (const UProg_AchievementSubsystem* Subsystem =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UProg_AchievementSubsystem>(WorldContext))
	{
		Current = Subsystem->GetChannelHitCount(ChannelTag);
	}
	return FMath::Clamp(static_cast<float>(Current) / static_cast<float>(Threshold), 0.f, 1.f);
}

FText UProg_Condition_BusCounter::GetConditionDescription_Implementation() const
{
	return FText::Format(LOCTEXT("BusCounter", "Channel '{0}' fired >= {1} times"),
		FText::FromString(ChannelTag.ToString()), FText::AsNumber(Threshold));
}

#undef LOCTEXT_NAMESPACE
