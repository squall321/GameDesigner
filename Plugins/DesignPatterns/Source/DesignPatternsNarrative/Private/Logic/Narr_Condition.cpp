// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Logic/Narr_Condition.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Clock/Seam_SimClock.h"

namespace Narr_ConditionOps
{
	/** Apply a comparison operator to two int64 operands. */
	static bool CompareInt(ENarr_CounterCompare Op, int64 Lhs, int64 Rhs)
	{
		switch (Op)
		{
		case ENarr_CounterCompare::Less:         return Lhs <  Rhs;
		case ENarr_CounterCompare::LessEqual:    return Lhs <= Rhs;
		case ENarr_CounterCompare::Equal:        return Lhs == Rhs;
		case ENarr_CounterCompare::GreaterEqual: return Lhs >= Rhs;
		case ENarr_CounterCompare::Greater:      return Lhs >  Rhs;
		case ENarr_CounterCompare::NotEqual:     return Lhs != Rhs;
		default:                                 return false;
		}
	}

	static const TCHAR* CompareToString(ENarr_CounterCompare Op)
	{
		switch (Op)
		{
		case ENarr_CounterCompare::Less:         return TEXT("<");
		case ENarr_CounterCompare::LessEqual:    return TEXT("<=");
		case ENarr_CounterCompare::Equal:        return TEXT("==");
		case ENarr_CounterCompare::GreaterEqual: return TEXT(">=");
		case ENarr_CounterCompare::Greater:      return TEXT(">");
		case ENarr_CounterCompare::NotEqual:     return TEXT("!=");
		default:                                 return TEXT("?");
		}
	}
}

// --- UNarr_Condition (base) ----------------------------------------------------------------------

bool UNarr_Condition::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& /*Source*/) const
{
	// Base "always pass" gate (subject to bInvert). Subclasses override with real logic.
	return Finalize(true);
}

FString UNarr_Condition::DescribeCondition() const
{
	return GetClass() ? GetClass()->GetName() : TEXT("<null condition>");
}

INarr_StoryConditionSource* UNarr_Condition::GetSource(const TScriptInterface<INarr_StoryConditionSource>& Source)
{
	return Source ? static_cast<INarr_StoryConditionSource*>(Source.GetInterface()) : nullptr;
}

// --- UNarr_Condition_Flag ------------------------------------------------------------------------

bool UNarr_Condition_Flag::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	INarr_StoryConditionSource* Src = GetSource(Source);
	if (!Src)
	{
		return Finalize(bDefaultWhenNoSource);
	}
	if (!FlagKey.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("UNarr_Condition_Flag with no FlagKey -> default."));
		return Finalize(bDefaultWhenNoSource);
	}

	const bool bActual = Src->QueryFlag(FlagKey, /*bDefault=*/false);
	return Finalize(bActual == bExpected);
}

FString UNarr_Condition_Flag::DescribeCondition() const
{
	return FString::Printf(TEXT("%sFlag(%s == %s)"),
		bInvert ? TEXT("!") : TEXT(""),
		*FlagKey.ToString(),
		bExpected ? TEXT("true") : TEXT("false"));
}

// --- UNarr_Condition_Counter ---------------------------------------------------------------------

bool UNarr_Condition_Counter::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	INarr_StoryConditionSource* Src = GetSource(Source);
	if (!Src)
	{
		return Finalize(bDefaultWhenNoSource);
	}
	if (!CounterKey.IsValid())
	{
		return Finalize(bDefaultWhenNoSource);
	}

	const int64 Value = Src->QueryCounter(CounterKey, /*Default=*/0);
	return Finalize(Narr_ConditionOps::CompareInt(Compare, Value, Threshold));
}

FString UNarr_Condition_Counter::DescribeCondition() const
{
	return FString::Printf(TEXT("%sCounter(%s %s %lld)"),
		bInvert ? TEXT("!") : TEXT(""),
		*CounterKey.ToString(),
		Narr_ConditionOps::CompareToString(Compare),
		Threshold);
}

// --- UNarr_Condition_BeatState -------------------------------------------------------------------

bool UNarr_Condition_BeatState::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	INarr_StoryConditionSource* Src = GetSource(Source);
	if (!Src)
	{
		return Finalize(bDefaultWhenNoSource);
	}
	if (!BeatOrArcTag.IsValid())
	{
		return Finalize(bDefaultWhenNoSource);
	}

	const bool bRaw = bRequireCompleted
		? Src->IsBeatCompleted(BeatOrArcTag)
		: Src->IsBeatActive(BeatOrArcTag);
	return Finalize(bRaw);
}

FString UNarr_Condition_BeatState::DescribeCondition() const
{
	return FString::Printf(TEXT("%sBeat(%s %s)"),
		bInvert ? TEXT("!") : TEXT(""),
		*BeatOrArcTag.ToString(),
		bRequireCompleted ? TEXT("completed") : TEXT("active"));
}

// --- UNarr_Condition_TimeOfDay -------------------------------------------------------------------

ISeam_SimClock* UNarr_Condition_TimeOfDay::ResolveClock(
	const TScriptInterface<INarr_StoryConditionSource>& Source, UObject*& OutClockObject)
{
	OutClockObject = nullptr;

	// The condition reads time via the locator-registered clock. We need a world context to reach the
	// GameInstance subsystem; the condition source object (story director / runner) provides one.
	UObject* WorldContext = Source.GetObject();
	if (!WorldContext)
	{
		return nullptr;
	}

	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(WorldContext);
	if (!Locator)
	{
		return nullptr;
	}

	// DP.Service.Clock is the core convention under which a project (e.g. the Survival day-night clock)
	// registers its ISeam_SimClock. Absent it, time gates fail closed.
	static const FGameplayTag ClockServiceTag =
		FGameplayTag::RequestGameplayTag(TEXT("DP.Service.Clock"), /*bErrorIfNotFound*/ false);
	if (!ClockServiceTag.IsValid())
	{
		return nullptr;
	}

	UObject* Provider = Locator->ResolveService(ClockServiceTag);
	if (Provider && Provider->Implements<USeam_SimClock>())
	{
		OutClockObject = Provider;
		return Cast<ISeam_SimClock>(Provider);
	}
	return nullptr;
}

bool UNarr_Condition_TimeOfDay::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	UObject* ClockObject = nullptr;
	const ISeam_SimClock* Clock = ResolveClock(Source, ClockObject);

	// Fail closed when there is no clock: a time gate without a clock is unsatisfiable.
	if (!Clock || !ClockObject)
	{
		UE_LOG(LogDP, Verbose, TEXT("UNarr_Condition_TimeOfDay: no clock available; failing closed."));
		return Finalize(false);
	}

	// ISeam_SimClock methods are BlueprintNativeEvent — call through the Execute_ thunk.
	const float Now = FMath::Frac(FMath::Max(0.f, ISeam_SimClock::Execute_GetNormalizedTimeOfDay(ClockObject)));
	const float Start = FMath::Clamp(StartTimeOfDay, 0.f, 1.f);
	const float End = FMath::Clamp(EndTimeOfDay, 0.f, 1.f);

	bool bInside;
	if (Start <= End)
	{
		// Normal (non-wrapping) window [Start, End).
		bInside = (Now >= Start && Now < End);
	}
	else
	{
		// Wrap-around window (e.g. a night window 0.8..0.2): inside means after Start OR before End.
		bInside = (Now >= Start || Now < End);
	}

	return Finalize(bInside);
}

FString UNarr_Condition_TimeOfDay::DescribeCondition() const
{
	return FString::Printf(TEXT("%sTimeOfDay[%.3f, %.3f)"),
		bInvert ? TEXT("!") : TEXT(""), StartTimeOfDay, EndTimeOfDay);
}

// --- UNarr_Condition_Composite -------------------------------------------------------------------

bool UNarr_Condition_Composite::IsMet_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	// Vacuous truth: an empty composite passes (before bInvert).
	if (Children.Num() == 0)
	{
		return Finalize(true);
	}

	if (Logic == ENarr_ConditionLogic::All)
	{
		for (const TObjectPtr<UNarr_Condition>& Child : Children)
		{
			// A null child is an absent sub-gate (passes); only a real failing child fails the All.
			if (Child && !Child->IsMet(Source))
			{
				return Finalize(false);
			}
		}
		return Finalize(true);
	}

	// Any
	for (const TObjectPtr<UNarr_Condition>& Child : Children)
	{
		if (Child && Child->IsMet(Source))
		{
			return Finalize(true);
		}
	}
	return Finalize(false);
}

FString UNarr_Condition_Composite::DescribeCondition() const
{
	TArray<FString> Parts;
	Parts.Reserve(Children.Num());
	for (const TObjectPtr<UNarr_Condition>& Child : Children)
	{
		Parts.Add(Child ? Child->DescribeCondition() : TEXT("<null>"));
	}
	const TCHAR* Join = (Logic == ENarr_ConditionLogic::All) ? TEXT(" && ") : TEXT(" || ");
	return FString::Printf(TEXT("%s(%s)"),
		bInvert ? TEXT("!Composite") : TEXT("Composite"),
		*FString::Join(Parts, Join));
}
