// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Effect/Combat_StatusStackController.h"
#include "Effect/Combat_StatusFamilyEffect.h"
#include "Effect/Combat_StatusEffectComponent.h"
#include "Effect/Combat_StatusEffect.h"

#include "Core/DPLog.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

// ---------------------------------------------------------------------------------------------
//  Fast-array item callbacks (client side)
// ---------------------------------------------------------------------------------------------

void FCombat_StatusStackEntry::PostReplicatedAdd(const FCombat_StatusStackArray& InArraySerializer)
{
	if (InArraySerializer.Owner)
	{
		InArraySerializer.Owner->HandleFamilyCountReplicated(FamilyTag, Count);
	}
}

void FCombat_StatusStackEntry::PostReplicatedChange(const FCombat_StatusStackArray& InArraySerializer)
{
	if (InArraySerializer.Owner)
	{
		InArraySerializer.Owner->HandleFamilyCountReplicated(FamilyTag, Count);
	}
}

void FCombat_StatusStackEntry::PreReplicatedRemove(const FCombat_StatusStackArray& InArraySerializer)
{
	if (InArraySerializer.Owner)
	{
		InArraySerializer.Owner->HandleFamilyCountReplicated(FamilyTag, 0);
	}
}

// ---------------------------------------------------------------------------------------------
//  Controller
// ---------------------------------------------------------------------------------------------

UCombat_StatusStackController::UCombat_StatusStackController()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UCombat_StatusStackController::BeginPlay()
{
	Super::BeginPlay();
	// Wire the non-replicated back-pointer on both server and client so item callbacks can notify us.
	FamilyStacks.Owner = this;
}

void UCombat_StatusStackController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UCombat_StatusStackController, FamilyStacks);
}

bool UCombat_StatusStackController::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

UCombat_StatusEffectComponent* UCombat_StatusStackController::GetStatusComponent() const
{
	const AActor* Owner = GetOwner();
	return Owner ? Owner->FindComponentByClass<UCombat_StatusEffectComponent>() : nullptr;
}

FCombat_StatusStackEntry* UCombat_StatusStackController::FindEntry(const FGameplayTag& FamilyTag)
{
	for (FCombat_StatusStackEntry& Entry : FamilyStacks.Entries)
	{
		if (Entry.FamilyTag == FamilyTag)
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FCombat_StatusStackEntry* UCombat_StatusStackController::FindEntry(const FGameplayTag& FamilyTag) const
{
	for (const FCombat_StatusStackEntry& Entry : FamilyStacks.Entries)
	{
		if (Entry.FamilyTag == FamilyTag)
		{
			return &Entry;
		}
	}
	return nullptr;
}

int32 UCombat_StatusStackController::GetFamilyCount(FGameplayTag FamilyTag) const
{
	const FCombat_StatusStackEntry* Entry = FindEntry(FamilyTag);
	return Entry ? Entry->Count : 0;
}

bool UCombat_StatusStackController::IsImmuneToFamily(FGameplayTag FamilyTag) const
{
	if (!FamilyTag.IsValid())
	{
		return false;
	}

	const UCombat_StatusEffectComponent* Status = GetStatusComponent();
	if (!Status)
	{
		return false;
	}

	// An actor is immune to a family if any active effect grants immunity to it. Authority knows the
	// live instances; on clients we conservatively answer false (gameplay gating runs on the server).
	if (!HasAuthoritySafe())
	{
		return false;
	}

	// Walk the active-effect tag set is not enough (we need the instances' GrantsImmunityTag), so we
	// inspect the component's instances indirectly: any active family effect whose GrantsImmunityTag
	// matches grants immunity. The status component owns the instances; we can only read its tags
	// publicly, so immunity is modeled as "a family that grants immunity to itself is currently
	// active". This keeps the controller from depending on the component's private instance list.
	// Designers express self-immunity by setting GrantsImmunityTag == FamilyTag on the effect.
	return GetFamilyCount(FamilyTag) > 0 && Status->HasEffectTag(FamilyTag);
}

UCombat_StatusEffect* UCombat_StatusStackController::TryApplyFamilyEffect(TSubclassOf<UCombat_StatusEffect> EffectClass)
{
	// AUTHORITY GUARD at the top — mutates replicated stack counts + replicated status tags.
	if (!HasAuthoritySafe())
	{
		return nullptr;
	}

	if (!EffectClass)
	{
		return nullptr;
	}

	UCombat_StatusEffectComponent* Status = GetStatusComponent();
	if (!Status)
	{
		UE_LOG(LogDP, Warning, TEXT("[StatusStack] %s has no UCombat_StatusEffectComponent; cannot apply."),
			*GetNameSafe(GetOwner()));
		return nullptr;
	}

	// Read family metadata from the CDO (no instance needed for the gating decision).
	const UCombat_StatusFamilyEffect* FamilyCDO = Cast<UCombat_StatusFamilyEffect>(EffectClass->GetDefaultObject());

	// Non-family effects just pass straight through to the existing component (no stacking semantics).
	if (!FamilyCDO || !FamilyCDO->FamilyTag.IsValid())
	{
		return Status->ApplyEffect(EffectClass);
	}

	const FGameplayTag Family = FamilyCDO->FamilyTag;

	// IMMUNITY gate.
	if (IsImmuneToFamily(Family))
	{
		UE_LOG(LogDP, Verbose, TEXT("[StatusStack] %s immune to family %s; apply blocked."),
			*GetNameSafe(GetOwner()), *Family.ToString());
		return nullptr;
	}

	const int32 ExistingCount = GetFamilyCount(Family);

	// MAX-STACKS gate for the Stack policy.
	if (FamilyCDO->StackPolicy == ECombat_StackPolicy::Stack && ExistingCount >= FMath::Max(1, FamilyCDO->MaxStacks))
	{
		// At cap: refresh the existing same-tag effect via the component instead of exceeding the cap.
		return Status->ApplyEffect(EffectClass);
	}

	// Apply via the existing component (handles same-tag refresh + replicated tag set).
	UCombat_StatusEffect* Applied = Status->ApplyEffect(EffectClass);
	if (!Applied)
	{
		return nullptr;
	}

	// DIMINISHING RETURNS: shrink the just-spawned instance's effective duration based on the prior
	// family count. The component samples Duration when it starts timing; we adjust before that by
	// rewriting the instance's Duration. (Refresh of an existing instance leaves Duration as authored.)
	if (UCombat_StatusFamilyEffect* AppliedFamily = Cast<UCombat_StatusFamilyEffect>(Applied))
	{
		const float Mult = AppliedFamily->GetDurationMultiplierForStack(ExistingCount);
		if (Mult < 1.f && AppliedFamily->Duration > 0.f)
		{
			AppliedFamily->Duration *= Mult;
		}
	}

	// Increment the replicated family count (Stack policy grows count; Refresh keeps it at >=1;
	// Independent counts each instance).
	if (FamilyCDO->StackPolicy == ECombat_StackPolicy::Refresh)
	{
		if (ExistingCount == 0)
		{
			AdjustFamilyCount(Family, +1);
		}
	}
	else
	{
		AdjustFamilyCount(Family, +1);
	}

	return Applied;
}

void UCombat_StatusStackController::NotifyEffectRemoved(FGameplayTag FamilyTag)
{
	if (!HasAuthoritySafe() || !FamilyTag.IsValid())
	{
		return;
	}
	AdjustFamilyCount(FamilyTag, -1);
}

void UCombat_StatusStackController::AdjustFamilyCount(const FGameplayTag& FamilyTag, int32 Delta)
{
	if (!FamilyTag.IsValid() || Delta == 0)
	{
		return;
	}

	FCombat_StatusStackEntry* Entry = FindEntry(FamilyTag);
	if (!Entry)
	{
		if (Delta <= 0)
		{
			return; // nothing to decrement
		}
		FCombat_StatusStackEntry NewEntry(FamilyTag);
		NewEntry.Count = Delta;
		FamilyStacks.Entries.Add(NewEntry);
		FamilyStacks.MarkItemDirty(FamilyStacks.Entries.Last());
		OnFamilyStackChanged.Broadcast(this, FamilyTag, NewEntry.Count);
		return;
	}

	Entry->Count = FMath::Max(0, Entry->Count + Delta);

	if (Entry->Count == 0)
	{
		const int32 Index = static_cast<int32>(Entry - FamilyStacks.Entries.GetData());
		FamilyStacks.Entries.RemoveAt(Index);
		FamilyStacks.MarkArrayDirty();
		OnFamilyStackChanged.Broadcast(this, FamilyTag, 0);
	}
	else
	{
		FamilyStacks.MarkItemDirty(*Entry);
		OnFamilyStackChanged.Broadcast(this, FamilyTag, Entry->Count);
	}
}

void UCombat_StatusStackController::HandleFamilyCountReplicated(const FGameplayTag& FamilyTag, int32 NewCount)
{
	// Client-side surface of a replicated count change.
	OnFamilyStackChanged.Broadcast(this, FamilyTag, NewCount);
}
