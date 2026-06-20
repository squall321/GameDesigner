// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Dialogue/Narr_StoryConditionTypes.h"
#include "Core/DPLog.h"

// ---------------------------------------------------------------------------------------------
//  UNarr_Effect (base)
// ---------------------------------------------------------------------------------------------

INarr_StoryConditionSource* UNarr_Effect::GetSource(const TScriptInterface<INarr_StoryConditionSource>& Source)
{
	return Source ? static_cast<INarr_StoryConditionSource*>(Source.GetInterface()) : nullptr;
}

void UNarr_Effect::Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& /*Source*/) const
{
	// Base effect does nothing; subclasses override.
}

FString UNarr_Effect::DescribeEffect() const
{
	return GetClass() ? GetClass()->GetName() : TEXT("<null effect>");
}

// ---------------------------------------------------------------------------------------------
//  UNarr_Effect_SetFlag
// ---------------------------------------------------------------------------------------------

void UNarr_Effect_SetFlag::Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	INarr_StoryConditionSource* Src = GetSource(Source);
	if (!Src || !FlagKey.IsValid())
	{
		return;
	}
	// ApplyFlag is authority-guarded by the source; safe to call from any machine.
	Src->ApplyFlag(FlagKey, bValue);
}

FString UNarr_Effect_SetFlag::DescribeEffect() const
{
	return FString::Printf(TEXT("SetFlag(%s = %s)"),
		*FlagKey.ToString(), bValue ? TEXT("true") : TEXT("false"));
}

// ---------------------------------------------------------------------------------------------
//  UNarr_Effect_AddCounter
// ---------------------------------------------------------------------------------------------

void UNarr_Effect_AddCounter::Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	INarr_StoryConditionSource* Src = GetSource(Source);
	if (!Src || !CounterKey.IsValid() || Delta == 0)
	{
		return;
	}
	Src->ApplyCounter(CounterKey, Delta);
}

FString UNarr_Effect_AddCounter::DescribeEffect() const
{
	return FString::Printf(TEXT("AddCounter(%s += %lld)"), *CounterKey.ToString(), Delta);
}
