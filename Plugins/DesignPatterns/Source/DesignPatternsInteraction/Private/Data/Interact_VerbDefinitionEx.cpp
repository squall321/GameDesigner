// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Data/Interact_VerbDefinitionEx.h"

UInteract_VerbDefinitionEx::UInteract_VerbDefinitionEx()
{
	// Default to instant tap; designers pick a richer mode per verb asset.
	ActivationMode = EInteract_ActivationMode::Tap;
}

EInteract_ActivationMode UInteract_VerbDefinitionEx::ResolveActivationMode(const UInteract_VerbDefinition* Def)
{
	if (const UInteract_VerbDefinitionEx* Ex = Cast<UInteract_VerbDefinitionEx>(Def))
	{
		return Ex->ActivationMode;
	}

	// Base-class verbs only know instant vs hold; derive accordingly so an un-extended verb still
	// activates correctly through the input driver.
	if (Def && Def->bHoldToActivate)
	{
		return EInteract_ActivationMode::Hold;
	}
	return EInteract_ActivationMode::Tap;
}
