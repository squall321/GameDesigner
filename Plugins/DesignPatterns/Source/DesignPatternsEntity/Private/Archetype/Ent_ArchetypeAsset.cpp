// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Archetype/Ent_ArchetypeAsset.h"
#include "Trait/Ent_Trait.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

namespace
{
	/** Shared PrimaryAssetType bucket for all entity archetypes. */
	const FName GEnt_ArchetypeAssetType(TEXT("Ent.Archetype"));
}

bool UEnt_ArchetypeAsset::CollectChain(TArray<const UEnt_ArchetypeAsset*>& OutChain,
	TSet<const UEnt_ArchetypeAsset*>& Visited) const
{
	if (Visited.Contains(this))
	{
		// Cycle: refuse to add this node again.
		UE_LOG(LogDP, Error, TEXT("UEnt_ArchetypeAsset '%s' has a cyclic ParentArchetype chain."), *GetName());
		return false;
	}
	Visited.Add(this);

	bool bOk = true;
	if (!ParentArchetype.IsNull())
	{
		// Synchronous load is acceptable: archetype resolution happens at spawn/author time, not per frame.
		if (const UEnt_ArchetypeAsset* Parent = ParentArchetype.LoadSynchronous())
		{
			bOk = Parent->CollectChain(OutChain, Visited);
		}
		else
		{
			UE_LOG(LogDP, Warning, TEXT("UEnt_ArchetypeAsset '%s' could not load its ParentArchetype."), *GetName());
		}
	}

	// Parents are appended before self, so a child extends/overrides its parents.
	OutChain.Add(this);
	return bOk;
}

void UEnt_ArchetypeAsset::GetResolvedDefaultTraits(TArray<UEnt_Trait*>& OutTraits) const
{
	TArray<const UEnt_ArchetypeAsset*> Chain;
	TSet<const UEnt_ArchetypeAsset*> Visited;
	CollectChain(Chain, Visited);

	for (const UEnt_ArchetypeAsset* Node : Chain)
	{
		if (!Node)
		{
			continue;
		}
		for (const TObjectPtr<UEnt_Trait>& Trait : Node->DefaultTraits)
		{
			if (UEnt_Trait* RawTrait = Trait.Get())
			{
				// Return the template; callers duplicate under the live owner before use.
				OutTraits.Add(RawTrait);
			}
		}
	}
}

void UEnt_ArchetypeAsset::GetResolvedDeclaredCapabilities(FGameplayTagContainer& OutCapabilities) const
{
	TArray<const UEnt_ArchetypeAsset*> Chain;
	TSet<const UEnt_ArchetypeAsset*> Visited;
	CollectChain(Chain, Visited);

	for (const UEnt_ArchetypeAsset* Node : Chain)
	{
		if (Node)
		{
			OutCapabilities.AppendTags(Node->DeclaredCapabilities);
		}
	}
}

void UEnt_ArchetypeAsset::GetEffectiveProvidedCapabilities(FGameplayTagContainer& OutCapabilities) const
{
	TArray<UEnt_Trait*> Traits;
	GetResolvedDefaultTraits(Traits);

	for (UEnt_Trait* Trait : Traits)
	{
		if (Trait)
		{
			// Use the interface entry point so Blueprint-overridden providers are honoured.
			IEnt_CapabilityProvider::Execute_GetProvidedCapabilities(Trait, OutCapabilities);
		}
	}
}

FName UEnt_ArchetypeAsset::GetDataAssetType_Implementation() const
{
	return GEnt_ArchetypeAssetType;
}

#if WITH_EDITOR
EDataValidationResult UEnt_ArchetypeAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// Null traits in the local list are an authoring error.
	for (int32 Index = 0; Index < DefaultTraits.Num(); ++Index)
	{
		if (!DefaultTraits[Index])
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("DefaultTraits[%d] is null on archetype '%s'."), Index, *GetName())));
			Result = EDataValidationResult::Invalid;
		}
	}

	// Detect a parent cycle by resolving the chain.
	TArray<const UEnt_ArchetypeAsset*> Chain;
	TSet<const UEnt_ArchetypeAsset*> Visited;
	if (!CollectChain(Chain, Visited))
	{
		Context.AddError(FText::FromString(
			FString::Printf(TEXT("Archetype '%s' has a cyclic ParentArchetype chain."), *GetName())));
		Result = EDataValidationResult::Invalid;
	}

	// Every DECLARED capability should actually be provided by some resolved trait.
	FGameplayTagContainer Declared;
	GetResolvedDeclaredCapabilities(Declared);
	FGameplayTagContainer Provided;
	GetEffectiveProvidedCapabilities(Provided);
	for (const FGameplayTag& Tag : Declared)
	{
		if (!Provided.HasTagExact(Tag))
		{
			Context.AddWarning(FText::FromString(FString::Printf(
				TEXT("Archetype '%s' declares capability '%s' but no resolved trait provides it."),
				*GetName(), *Tag.ToString())));
		}
	}

	return Result;
}
#endif
