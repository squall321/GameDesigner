// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Interact/Seam_InteractAvailability.h"

// No-op BlueprintNativeEvent default bodies. An implementer overrides only what it needs; the
// framework stays safe when the seam is absent (the consumer treats an unoverridden default as available).

bool ISeam_InteractAvailability::IsVerbAvailable_Implementation(
	FGameplayTag /*Verb*/, const TScriptInterface<ISeam_EntityIdentity>& /*Instigator*/, FGameplayTag& OutReasonTag) const
{
	// Default: available, no reason.
	OutReasonTag = FGameplayTag();
	return true;
}

FText ISeam_InteractAvailability::GetUnavailableReasonText_Implementation(
	FGameplayTag /*Verb*/, FGameplayTag /*ReasonTag*/) const
{
	// Default: nothing to add; the consumer substitutes its own per-reason fallback text.
	return FText::GetEmpty();
}
