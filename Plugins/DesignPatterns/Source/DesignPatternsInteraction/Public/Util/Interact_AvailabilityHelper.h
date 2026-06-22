// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Types/Interact_Types.h"
#include "Types/Interact_AvailabilityTypes.h"
#include "Interact_AvailabilityHelper.generated.h"

class ISeam_EntityIdentity;

/**
 * Stateless bridge between the optional ISeam_InteractAvailability seam and the Interaction module's
 * FInteract_VerbAvailability / FInteract_VerbMenu value types.
 *
 * This is the SINGLE place the Interaction module touches the availability seam: the interactor and
 * context-menu components only ever see the assembled FInteract_VerbMenu, never the seam itself, so
 * they remain free of any gameplay-system knowledge.
 *
 * Behaviour when the interactable does NOT implement ISeam_InteractAvailability: every verb comes
 * back bEnabled=true with empty reason — i.e. "available". The helper also resolves player-facing
 * reason text via the seam, falling back to a built-in per-reason default text table so a denial
 * always shows something sensible.
 */
UCLASS()
class DESIGNPATTERNSINTERACTION_API UInteract_AvailabilityHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Evaluate a single verb's availability against an interactable for the given instigator identity.
	 *
	 * @param Interactable  The object implementing IInteract_Interactable (actor or component).
	 * @param Verb          The verb to evaluate.
	 * @param ActionText    Player-facing action label for the verb (from the prompt), copied into the result.
	 * @param PromptStyleTag Optional style tag for the verb (from the verb definition).
	 * @param Instigator    The acting entity by identity (may be empty for an anonymous query).
	 * @return A fully populated FInteract_VerbAvailability (bEnabled true when usable or seam absent).
	 */
	UFUNCTION(BlueprintCallable, Category = "Interact|Availability")
	static FInteract_VerbAvailability EvaluateVerb(
		UObject* Interactable,
		FGameplayTag Verb,
		const FText& ActionText,
		FGameplayTag PromptStyleTag,
		const TScriptInterface<ISeam_EntityIdentity>& Instigator);

	/**
	 * Build the full verb menu for an interactable under Query.
	 *
	 * Resolution order:
	 *   1. If the interactable implements IInteract_MultiVerbProvider, that menu is the base surface.
	 *   2. Otherwise GetSupportedVerbs + per-verb GetInteractionPrompt builds the base surface.
	 *   3. Either way each verb is then folded through the ISeam_InteractAvailability seam.
	 *
	 * The instigator identity is resolved from Query.Instigator (its ISeam_EntityIdentity component,
	 * if any) so the availability system keys off a stable entity id, not the concrete pawn type.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interact|Availability")
	static void BuildVerbMenu(UObject* Interactable, const FInteract_Query& Query, FInteract_VerbMenu& Out);

	/**
	 * Default player-facing text for a DP.Interact.Reason.* tag, used when the seam returns empty text
	 * (or is absent). Returns a generic "Unavailable" for unknown reason tags.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Interact|Availability")
	static FText GetDefaultReasonText(FGameplayTag ReasonTag);

	/**
	 * Resolve the ISeam_EntityIdentity for an actor (the actor itself, or its first component that
	 * implements the seam), wrapped as a TScriptInterface. Returns an empty interface when none exists.
	 */
	static TScriptInterface<ISeam_EntityIdentity> ResolveInstigatorIdentity(AActor* InstigatorActor);
};
