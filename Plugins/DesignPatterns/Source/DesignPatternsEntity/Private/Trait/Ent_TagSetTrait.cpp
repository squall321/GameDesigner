// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Trait/Ent_TagSetTrait.h"
#include "DesignPatternsEntityTags.h"
#include "Core/DPLog.h"

UEnt_TagSetTrait::UEnt_TagSetTrait()
{
	// Identify this trait kind so consumers can resolve it by tag.
	CapabilityTag = EntNativeTags::Trait_TagSet;
	// Data-only: no per-frame work.
	bWantsTick = false;
}

void UEnt_TagSetTrait::OnTraitAdded_Implementation(UEnt_EntityComponent* OwningComponent_In)
{
	Super::OnTraitAdded_Implementation(OwningComponent_In);

	// Seed the live set from the authored initial tags (RestoreState may overwrite this afterwards).
	LiveTags = InitialTags;
}

bool UEnt_TagSetTrait::AddTag(FGameplayTag Tag)
{
	if (!Tag.IsValid() || LiveTags.HasTagExact(Tag))
	{
		return false;
	}
	LiveTags.AddTag(Tag);
	return true;
}

bool UEnt_TagSetTrait::RemoveTag(FGameplayTag Tag)
{
	if (!Tag.IsValid() || !LiveTags.HasTagExact(Tag))
	{
		return false;
	}
	LiveTags.RemoveTag(Tag);
	return true;
}

bool UEnt_TagSetTrait::HasTag(FGameplayTag Tag) const
{
	return Tag.IsValid() && LiveTags.HasTagExact(Tag);
}

bool UEnt_TagSetTrait::HasTagMatching(FGameplayTag Tag) const
{
	return Tag.IsValid() && LiveTags.HasTag(Tag);
}

void UEnt_TagSetTrait::SetTags(const FGameplayTagContainer& NewTags)
{
	LiveTags = NewTags;
}

void UEnt_TagSetTrait::SaveState_Implementation(FInstancedStruct& Out) const
{
	FEnt_TagSetTraitSave Record;
	Record.Tags = LiveTags;
	Out.InitializeAs<FEnt_TagSetTraitSave>(Record);
}

void UEnt_TagSetTrait::RestoreState_Implementation(const FInstancedStruct& In)
{
	// Tolerate empty/mismatched records: treat as "no saved state" and keep the seeded set.
	if (const FEnt_TagSetTraitSave* Record = In.GetPtr<FEnt_TagSetTraitSave>())
	{
		LiveTags = Record->Tags;
	}
	else if (In.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("UEnt_TagSetTrait::RestoreState ignored a record of type %s."),
			*GetNameSafe(const_cast<UScriptStruct*>(In.GetScriptStruct())));
	}
}
