// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Trait/Ent_Trait.h"
#include "Core/DPLog.h"

UEnt_Trait::UEnt_Trait()
{
}

void UEnt_Trait::OnTraitAdded_Implementation(UEnt_EntityComponent* OwningComponent_In)
{
	// Record the non-owning back-reference. Concrete traits override and call Super to keep it set.
	OwningComponent = OwningComponent_In;
}

void UEnt_Trait::OnTraitTick_Implementation(float /*DeltaSeconds*/)
{
	// Base trait is inert; stateful traits override.
}

void UEnt_Trait::OnTraitRemoved_Implementation()
{
	// Drop the back-reference so a lingering trait can't deref a stale component.
	OwningComponent.Reset();
}

void UEnt_Trait::SaveState_Implementation(FInstancedStruct& Out) const
{
	// Default: nothing durable to save.
	Out.Reset();
}

void UEnt_Trait::RestoreState_Implementation(const FInstancedStruct& /*In*/)
{
	// Default: nothing to restore.
}

void UEnt_Trait::GetProvidedCapabilities_Implementation(FGameplayTagContainer& OutCapabilities) const
{
	// Append (never reset) so callers can aggregate across many traits/providers.
	if (CapabilityTag.IsValid())
	{
		OutCapabilities.AddTag(CapabilityTag);
	}
	OutCapabilities.AppendTags(ProvidedCapabilities);
}

bool UEnt_Trait::HasCapability_Implementation(FGameplayTag InCapabilityTag) const
{
	return ProvidesCapability(InCapabilityTag);
}

UObject* UEnt_Trait::GetCapabilityObject_Implementation(FGameplayTag InCapabilityTag) const
{
	// A trait backs its own capabilities — the consumer casts the trait to the relevant domain seam.
	// const_cast is safe here: the seam contract hands back a mutable backing object to invoke.
	return ProvidesCapability(InCapabilityTag) ? const_cast<UEnt_Trait*>(this) : nullptr;
}

bool UEnt_Trait::ProvidesCapability(FGameplayTag InCapabilityTag) const
{
	if (!InCapabilityTag.IsValid())
	{
		return false;
	}
	return (CapabilityTag.IsValid() && CapabilityTag == InCapabilityTag)
		|| ProvidedCapabilities.HasTagExact(InCapabilityTag);
}
