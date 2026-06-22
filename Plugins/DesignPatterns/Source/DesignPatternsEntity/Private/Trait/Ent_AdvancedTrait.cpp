// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Trait/Ent_AdvancedTrait.h"
#include "Trait/Ent_TraitDefinition.h"
#include "Entity/Ent_EntityComponent.h"

#include "Core/DPLog.h"

UEnt_AdvancedTrait::UEnt_AdvancedTrait()
{
	// Advanced traits typically do not need ticking; subclasses opt in via bWantsTick.
	bWantsTick = false;
}

const UEnt_TraitDefinition* UEnt_AdvancedTrait::GetDefinition() const
{
	if (CachedDefinition)
	{
		return CachedDefinition;
	}
	if (!Definition.IsNull())
	{
		CachedDefinition = Definition.LoadSynchronous();
	}
	return CachedDefinition;
}

//~ State encode/decode -----------------------------------------------------------------------

FSeam_NetValue UEnt_AdvancedTrait::EncodeState(bool bEnabled, int32 StackCount)
{
	const int32 Stack = FMath::Max(1, StackCount);
	// bit 0 = enabled flag; remaining bits = stack count.
	const int64 Encoded = (static_cast<int64>(Stack) << 1) | (bEnabled ? 1 : 0);
	return FSeam_NetValue::MakeInt(Encoded);
}

bool UEnt_AdvancedTrait::DecodeEnabled(const FSeam_NetValue& Payload)
{
	if (!Payload.IsSet())
	{
		// Unset payload means "no explicit state written yet" -> treat as enabled (default).
		return true;
	}
	return (Payload.IntValue & 0x1) != 0;
}

int32 UEnt_AdvancedTrait::DecodeStack(const FSeam_NetValue& Payload)
{
	if (!Payload.IsSet())
	{
		return 1;
	}
	const int32 Stack = static_cast<int32>(Payload.IntValue >> 1);
	return FMath::Max(1, Stack);
}

void UEnt_AdvancedTrait::WriteState(bool bEnabled, int32 StackCount)
{
	UEnt_EntityComponent* Owner = GetOwningComponent();
	if (!Owner || !Owner->HasEntityAuthority())
	{
		return;
	}
	Owner->SetTraitStatePayload(CapabilityTag, EncodeState(bEnabled, StackCount));
}

//~ Public state API --------------------------------------------------------------------------

bool UEnt_AdvancedTrait::IsTraitEnabled() const
{
	const UEnt_EntityComponent* Owner = GetOwningComponent();
	if (!Owner)
	{
		return bStartEnabled;
	}
	return DecodeEnabled(Owner->GetTraitStatePayload(CapabilityTag));
}

int32 UEnt_AdvancedTrait::GetStackCount() const
{
	const UEnt_EntityComponent* Owner = GetOwningComponent();
	if (!Owner)
	{
		return 1;
	}
	return DecodeStack(Owner->GetTraitStatePayload(CapabilityTag));
}

void UEnt_AdvancedTrait::SetTraitEnabled(bool bEnabled)
{
	UEnt_EntityComponent* Owner = GetOwningComponent();
	if (!Owner || !Owner->HasEntityAuthority())
	{
		return;
	}
	// Refuse to enable while dependencies are unmet / a conflict exists.
	if (bEnabled && !AreDependenciesSatisfied())
	{
		UE_LOG(LogDP, Verbose, TEXT("[AdvancedTrait] '%s' cannot enable: dependencies unmet / conflict present."),
			*CapabilityTag.ToString());
		bEnabled = false;
	}
	WriteState(bEnabled, GetStackCount());

	// Fire the local hook on authority (clients fire it from OnEntityChanged when the rep arrives).
	if (bEnabled != bLastObservedEnabled)
	{
		bLastObservedEnabled = bEnabled;
		if (bEnabled) { Execute_OnTraitEnabled(this); } else { Execute_OnTraitDisabled(this); }
	}
}

void UEnt_AdvancedTrait::SetStackCount(int32 NewCount)
{
	UEnt_EntityComponent* Owner = GetOwningComponent();
	if (!Owner || !Owner->HasEntityAuthority())
	{
		return;
	}
	int32 Clamped = FMath::Max(1, NewCount);
	if (const UEnt_TraitDefinition* Def = GetDefinition())
	{
		if (Def->StackPolicy == EEnt_TraitStackPolicy::Stack)
		{
			Clamped = FMath::Clamp(Clamped, 1, FMath::Max(1, Def->MaxStackCount));
		}
		else
		{
			Clamped = 1; // Non-stacking kinds are always count 1.
		}
	}
	WriteState(IsTraitEnabled(), Clamped);
}

bool UEnt_AdvancedTrait::AreDependenciesSatisfied() const
{
	const UEnt_EntityComponent* Owner = GetOwningComponent();
	const UEnt_TraitDefinition* Def = GetDefinition();
	if (!Owner || !Def)
	{
		return true; // No definition = no constraints.
	}

	// Conflicts: the entity must not carry any conflicting trait kind.
	for (const FGameplayTag& Conflict : Def->ConflictingTraitTags)
	{
		if (Owner->FindTraitByTag(Conflict))
		{
			return false;
		}
	}
	// Dependencies: every required trait kind must be present.
	for (const FGameplayTag& Required : Def->RequiredTraitTags)
	{
		if (!Owner->FindTraitByTag(Required))
		{
			return false;
		}
	}
	return true;
}

//~ UEnt_Trait overrides ----------------------------------------------------------------------

void UEnt_AdvancedTrait::OnTraitAdded_Implementation(UEnt_EntityComponent* OwningComponent_In)
{
	Super::OnTraitAdded_Implementation(OwningComponent_In);

	// Authority seeds the initial replicated state; clients read whatever already replicated.
	if (OwningComponent_In && OwningComponent_In->HasEntityAuthority())
	{
		const bool bEnabled = bStartEnabled && AreDependenciesSatisfied();
		WriteState(bEnabled, 1);
		bLastObservedEnabled = bEnabled;
		if (bEnabled)
		{
			Execute_OnTraitEnabled(this);
		}
	}
	else
	{
		bLastObservedEnabled = IsTraitEnabled();
	}
}

void UEnt_AdvancedTrait::OnTraitRemoved_Implementation()
{
	if (bLastObservedEnabled)
	{
		bLastObservedEnabled = false;
		Execute_OnTraitDisabled(this);
	}
	Super::OnTraitRemoved_Implementation();
}

void UEnt_AdvancedTrait::GetProvidedCapabilities_Implementation(FGameplayTagContainer& OutCapabilities) const
{
	// Disabled trait advertises nothing through the capability seam.
	if (!IsTraitEnabled())
	{
		return;
	}
	Super::GetProvidedCapabilities_Implementation(OutCapabilities);
}
