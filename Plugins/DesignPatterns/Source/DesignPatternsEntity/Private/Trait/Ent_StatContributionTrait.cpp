// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Trait/Ent_StatContributionTrait.h"
#include "Entity/Ent_EntityComponent.h"
#include "Capability/Ent_CapabilityProvider.h"
#include "Stats/Seam_StatModifierSink.h"

#include "Core/DPLog.h"

UEnt_StatContributionTrait::UEnt_StatContributionTrait()
{
	bWantsTick = false;
}

void UEnt_StatContributionTrait::OnTraitAdded_Implementation(UEnt_EntityComponent* OwningComponent_In)
{
	Super::OnTraitAdded_Implementation(OwningComponent_In);

	// Re-derive whenever the entity's replicated state changes (covers client-side StatePayload reps and
	// trait-set changes that might bring/remove a dependency). Bound on server AND clients so both push
	// the identical derived group.
	if (OwningComponent_In && !bBoundToEntityChanged)
	{
		OwningComponent_In->OnEntityChanged.AddDynamic(this, &UEnt_StatContributionTrait::HandleEntityChanged);
		bBoundToEntityChanged = true;
	}

	RecomputeAndPush();
}

void UEnt_StatContributionTrait::OnTraitRemoved_Implementation()
{
	// Clear our contribution and unbind before the base tears down.
	if (UEnt_EntityComponent* Owner = GetOwningComponent())
	{
		if (UObject* SinkObj = IEnt_CapabilityProvider::Execute_GetCapabilityObject(Owner, StatSinkCapabilityTag))
		{
			if (SinkObj->Implements<USeam_StatModifierSink>())
			{
				ISeam_StatModifierSink::Execute_SetDerivedModifierGroup(SinkObj, SourceTag, TArray<FSeam_StatMod>());
			}
		}
		if (bBoundToEntityChanged)
		{
			Owner->OnEntityChanged.RemoveDynamic(this, &UEnt_StatContributionTrait::HandleEntityChanged);
			bBoundToEntityChanged = false;
		}
	}

	Super::OnTraitRemoved_Implementation();
}

void UEnt_StatContributionTrait::OnTraitEnabled_Implementation()
{
	RecomputeAndPush();
}

void UEnt_StatContributionTrait::OnTraitDisabled_Implementation()
{
	RecomputeAndPush();
}

void UEnt_StatContributionTrait::HandleEntityChanged(UEnt_EntityComponent* /*Component*/)
{
	RecomputeAndPush();
}

void UEnt_StatContributionTrait::RecomputeAndPush()
{
	UEnt_EntityComponent* Owner = GetOwningComponent();
	if (!Owner)
	{
		return;
	}

	// Resolve the stat sink per-use via the capability seam (never cache the pointer).
	UObject* SinkObj = IEnt_CapabilityProvider::Execute_GetCapabilityObject(Owner, StatSinkCapabilityTag);
	if (!SinkObj || !SinkObj->Implements<USeam_StatModifierSink>())
	{
		return;
	}

	// Build the derived group from REPLICATED inputs only. Disabled => empty group (clears contribution).
	TArray<FSeam_StatMod> Group;
	if (IsTraitEnabled())
	{
		const int32 Stack = GetStackCount();
		Group.Reserve(AuthoredMods.Num());
		for (const FSeam_StatMod& Authored : AuthoredMods)
		{
			FSeam_StatMod Mod = Authored;
			if (bScaleByStack && Stack > 1 && Mod.Magnitude.Type == ESeam_NetValueType::Float)
			{
				Mod.Magnitude = FSeam_NetValue::MakeFloat(Mod.Magnitude.FloatValue * static_cast<double>(Stack));
			}
			// Ensure the group is keyed under our source so it replaces atomically.
			Mod.SourceTag = SourceTag;
			Group.Add(Mod);
		}
	}

	// LOCAL-DERIVED path: no authority guard by seam contract; runs identically on server + clients.
	ISeam_StatModifierSink::Execute_SetDerivedModifierGroup(SinkObj, SourceTag, Group);
}
