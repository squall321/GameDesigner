// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Trait/Ent_StatBagTrait.h"
#include "DesignPatternsEntityTags.h"
#include "Core/DPLog.h"

UEnt_StatBagTrait::UEnt_StatBagTrait()
{
	CapabilityTag = EntNativeTags::Trait_StatBag;
	bWantsTick = false;
}

void UEnt_StatBagTrait::OnTraitAdded_Implementation(UEnt_EntityComponent* OwningComponent_In)
{
	Super::OnTraitAdded_Implementation(OwningComponent_In);

	// Seed defs + live values from the authored stats (RestoreState may overwrite values afterwards).
	StatDefs.Reset();
	LiveValues.Reset();
	for (const FEnt_StatDef& Def : InitialStats)
	{
		if (!Def.StatTag.IsValid())
		{
			UE_LOG(LogDP, Warning, TEXT("UEnt_StatBagTrait on %s has a stat with an invalid tag; skipped."),
				*GetNameSafe(GetOwningComponent()));
			continue;
		}
		StatDefs.Add(Def.StatTag, Def);
		LiveValues.Add(Def.StatTag, ClampForStat(Def.StatTag, Def.Value));
	}
}

float UEnt_StatBagTrait::ClampForStat(FGameplayTag StatTag, float Value) const
{
	if (const FEnt_StatDef* Def = StatDefs.Find(StatTag))
	{
		if (Def->bClamp)
		{
			const float Lo = FMath::Min(Def->MinValue, Def->MaxValue);
			const float Hi = FMath::Max(Def->MinValue, Def->MaxValue);
			return FMath::Clamp(Value, Lo, Hi);
		}
	}
	return Value;
}

float UEnt_StatBagTrait::GetStat(FGameplayTag StatTag, float DefaultValue) const
{
	const float* Found = LiveValues.Find(StatTag);
	return Found ? *Found : DefaultValue;
}

bool UEnt_StatBagTrait::TryGetStat(FGameplayTag StatTag, float& OutValue) const
{
	if (const float* Found = LiveValues.Find(StatTag))
	{
		OutValue = *Found;
		return true;
	}
	OutValue = 0.f;
	return false;
}

float UEnt_StatBagTrait::SetStat(FGameplayTag StatTag, float NewValue)
{
	if (!StatTag.IsValid())
	{
		return 0.f;
	}
	const float Clamped = ClampForStat(StatTag, NewValue);
	LiveValues.Add(StatTag, Clamped);
	return Clamped;
}

float UEnt_StatBagTrait::AddToStat(FGameplayTag StatTag, float Delta)
{
	if (!StatTag.IsValid())
	{
		return 0.f;
	}
	const float Current = GetStat(StatTag, 0.f);
	return SetStat(StatTag, Current + Delta);
}

float UEnt_StatBagTrait::GetStatNormalized(FGameplayTag StatTag) const
{
	const float* Found = LiveValues.Find(StatTag);
	if (!Found)
	{
		return 0.f;
	}
	if (const FEnt_StatDef* Def = StatDefs.Find(StatTag))
	{
		if (Def->bClamp)
		{
			const float Lo = FMath::Min(Def->MinValue, Def->MaxValue);
			const float Hi = FMath::Max(Def->MinValue, Def->MaxValue);
			const float Range = Hi - Lo;
			if (Range > UE_SMALL_NUMBER)
			{
				return FMath::Clamp((*Found - Lo) / Range, 0.f, 1.f);
			}
		}
	}
	// Unclamped or degenerate range: no meaningful normalization.
	return 0.f;
}

void UEnt_StatBagTrait::GetStatTags(FGameplayTagContainer& OutTags) const
{
	for (const TPair<FGameplayTag, float>& Pair : LiveValues)
	{
		OutTags.AddTag(Pair.Key);
	}
}

void UEnt_StatBagTrait::SaveState_Implementation(FInstancedStruct& Out) const
{
	FEnt_StatBagTraitSave Record;
	Record.Values = LiveValues;
	Out.InitializeAs<FEnt_StatBagTraitSave>(Record);
}

void UEnt_StatBagTrait::RestoreState_Implementation(const FInstancedStruct& In)
{
	if (const FEnt_StatBagTraitSave* Record = In.GetPtr<FEnt_StatBagTraitSave>())
	{
		// Re-clamp restored values against authored ranges in case the asset's clamps changed since save.
		for (const TPair<FGameplayTag, float>& Pair : Record->Values)
		{
			if (Pair.Key.IsValid())
			{
				LiveValues.Add(Pair.Key, ClampForStat(Pair.Key, Pair.Value));
			}
		}
	}
	else if (In.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("UEnt_StatBagTrait::RestoreState ignored a record of type %s."),
			*GetNameSafe(const_cast<UScriptStruct*>(In.GetScriptStruct())));
	}
}

float UEnt_StatBagTrait::GetNeedNormalized_Implementation(FGameplayTag NeedTag) const
{
	// A need's satisfaction is exactly the stat's normalized value (only meaningful for clamped stats).
	return GetStatNormalized(NeedTag);
}

bool UEnt_StatBagTrait::SupportsNeed_Implementation(FGameplayTag NeedTag) const
{
	// Only clamped stats answer as needs (an unclamped stat has no normalized satisfaction).
	const FEnt_StatDef* Def = StatDefs.Find(NeedTag);
	return Def != nullptr && Def->bClamp && LiveValues.Contains(NeedTag);
}

void UEnt_StatBagTrait::GetSupportedNeeds_Implementation(FGameplayTagContainer& OutNeeds) const
{
	for (const TPair<FGameplayTag, FEnt_StatDef>& Pair : StatDefs)
	{
		if (Pair.Value.bClamp && LiveValues.Contains(Pair.Key))
		{
			OutNeeds.AddTag(Pair.Key);
		}
	}
}
