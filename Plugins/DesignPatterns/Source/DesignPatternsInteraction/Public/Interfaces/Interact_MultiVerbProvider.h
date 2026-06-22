// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Types/Interact_Types.h"
#include "Types/Interact_AvailabilityTypes.h"
#include "Interact_MultiVerbProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI, meta = (CannotImplementInterfaceInBlueprint = false))
class UInteract_MultiVerbProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * OPTIONAL companion interface an interactable MAY implement alongside IInteract_Interactable when
 * it wants to express richer per-verb metadata than the flat IInteract_Interactable::GetSupportedVerbs
 * can (per-verb action text, default selection, per-verb enabled state it computes itself).
 *
 * The interactor checks Implements<UInteract_MultiVerbProvider>() and, when present, uses GetVerbMenu
 * as the authoritative verb surface; when absent it falls back to the UNCHANGED GetSupportedVerbs +
 * GetInteractionPrompt path. This keeps the shipped IInteract_Interactable contract untouched while
 * letting advanced interactables drive a multi-verb context menu directly.
 *
 * Read-only / side-effect free: it runs on clients for menu display and on the server for re-derivation.
 */
class DESIGNPATTERNSINTERACTION_API IInteract_MultiVerbProvider
{
	GENERATED_BODY()

public:
	/**
	 * Produce the full verb menu for the given query. Implementations should fill OutMenu.Verbs with
	 * one entry per offered verb (action text + enabled state) and set OutMenu.DefaultVerbIndex.
	 * The interactor still folds in the ISeam_InteractAvailability seam afterwards, so an
	 * implementation may leave bEnabled=true and let the availability seam gate it.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interact|MultiVerb")
	void GetVerbMenu(const FInteract_Query& Query, FInteract_VerbMenu& OutMenu) const;
};
