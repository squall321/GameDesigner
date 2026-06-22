// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Util/Interact_AvailabilityHelper.h"

#include "Interfaces/Interact_Interactable.h"
#include "Interfaces/Interact_MultiVerbProvider.h"
#include "DesignPatternsInteractionModule.h"

#include "Interact/Seam_InteractAvailability.h"
#include "Identity/Seam_EntityIdentity.h"

#include "Core/DPLog.h"

#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "Interact"

FInteract_VerbAvailability UInteract_AvailabilityHelper::EvaluateVerb(
	UObject* Interactable,
	FGameplayTag Verb,
	const FText& ActionText,
	FGameplayTag PromptStyleTag,
	const TScriptInterface<ISeam_EntityIdentity>& Instigator)
{
	FInteract_VerbAvailability Result;
	Result.Verb = Verb;
	Result.ActionText = ActionText;
	Result.PromptStyleTag = PromptStyleTag;
	Result.bEnabled = true;

	if (!Interactable)
	{
		return Result;
	}

	// The availability seam is OPTIONAL. When the interactable does not implement it, the verb is
	// considered available — the interactor stays ignorant of any gameplay gating system.
	if (!Interactable->Implements<USeam_InteractAvailability>())
	{
		return Result;
	}

	FGameplayTag ReasonTag;
	const bool bAvailable = ISeam_InteractAvailability::Execute_IsVerbAvailable(
		Interactable, Verb, Instigator, ReasonTag);

	if (bAvailable)
	{
		return Result;
	}

	Result.bEnabled = false;
	Result.ReasonTag = ReasonTag;

	// Prefer the seam's own text; fall back to the built-in per-reason default so a denial always
	// has something readable to show.
	FText SeamText = ISeam_InteractAvailability::Execute_GetUnavailableReasonText(Interactable, Verb, ReasonTag);
	Result.ReasonText = SeamText.IsEmpty() ? GetDefaultReasonText(ReasonTag) : SeamText;

	return Result;
}

void UInteract_AvailabilityHelper::BuildVerbMenu(UObject* Interactable, const FInteract_Query& Query, FInteract_VerbMenu& Out)
{
	Out = FInteract_VerbMenu();

	if (!Interactable || !Interactable->Implements<UInteract_Interactable>())
	{
		return;
	}

	// The menu's target is the actor that owns the interactable (the actor itself, or the component's owner).
	if (const AActor* AsActor = Cast<AActor>(Interactable))
	{
		Out.Target = const_cast<AActor*>(AsActor);
	}
	else if (const UActorComponent* AsComp = Cast<UActorComponent>(Interactable))
	{
		Out.Target = AsComp->GetOwner();
	}

	const TScriptInterface<ISeam_EntityIdentity> Instigator = ResolveInstigatorIdentity(Query.Instigator.Get());

	// Path 1: a multi-verb provider supplies the base surface verbatim (per-verb action text/default).
	if (Interactable->Implements<UInteract_MultiVerbProvider>())
	{
		FInteract_VerbMenu Provided;
		IInteract_MultiVerbProvider::Execute_GetVerbMenu(Interactable, Query, Provided);

		Out.Verbs.Reserve(Provided.Verbs.Num());
		for (const FInteract_VerbAvailability& Base : Provided.Verbs)
		{
			// Fold the seam over each provided verb. If the provider already disabled it, keep that
			// (the seam can only further constrain, never re-enable an explicitly disabled verb).
			FInteract_VerbAvailability Folded = EvaluateVerb(
				Interactable, Base.Verb, Base.ActionText, Base.PromptStyleTag, Instigator);
			if (!Base.bEnabled)
			{
				Folded.bEnabled = false;
				if (!Base.ReasonTag.IsValid())
				{
					// Preserve provider's own reason if it set one.
				}
				else
				{
					Folded.ReasonTag = Base.ReasonTag;
					Folded.ReasonText = Base.ReasonText.IsEmpty() ? GetDefaultReasonText(Base.ReasonTag) : Base.ReasonText;
				}
			}
			Out.Verbs.Add(MoveTemp(Folded));
		}

		Out.DefaultVerbIndex = Provided.DefaultVerbIndex;
	}
	else
	{
		// Path 2: flat IInteract_Interactable surface — one entry per supported verb, action text and
		// style derived from the per-verb prompt.
		FGameplayTagContainer Supported;
		IInteract_Interactable::Execute_GetSupportedVerbs(Interactable, Supported);

		Out.Verbs.Reserve(Supported.Num());
		for (const FGameplayTag& Verb : Supported)
		{
			if (!Verb.IsValid())
			{
				continue;
			}

			// Build a per-verb query so the prompt reflects the specific verb's label.
			FInteract_Query VerbQuery = Query;
			VerbQuery.DesiredVerb = Verb;
			const FInteract_PromptInfo Prompt = IInteract_Interactable::Execute_GetInteractionPrompt(Interactable, VerbQuery);

			// FInteract_PromptInfo carries no style tag; the verb definition's PromptStyleTag is resolved
			// by the UI layer from the verb tag, so leave the style tag empty here.
			FInteract_VerbAvailability Entry = EvaluateVerb(
				Interactable, Verb, Prompt.Action, FGameplayTag(), Instigator);

			// Honour the prompt's own enabled flag (an interactable can grey a verb without the seam).
			if (!Prompt.bEnabled && Entry.bEnabled)
			{
				Entry.bEnabled = false;
				Entry.ReasonText = GetDefaultReasonText(Entry.ReasonTag);
			}

			Out.Verbs.Add(MoveTemp(Entry));
		}

		// Default to the first ENABLED verb, else the first verb, else none.
		Out.DefaultVerbIndex = INDEX_NONE;
		for (int32 Index = 0; Index < Out.Verbs.Num(); ++Index)
		{
			if (Out.Verbs[Index].bEnabled)
			{
				Out.DefaultVerbIndex = Index;
				break;
			}
		}
		if (Out.DefaultVerbIndex == INDEX_NONE && Out.Verbs.Num() > 0)
		{
			Out.DefaultVerbIndex = 0;
		}
	}
}

FText UInteract_AvailabilityHelper::GetDefaultReasonText(FGameplayTag ReasonTag)
{
	if (ReasonTag == InteractNativeTags::Reason_Locked)
	{
		return LOCTEXT("Reason_Locked", "Locked");
	}
	if (ReasonTag == InteractNativeTags::Reason_NoKey)
	{
		return LOCTEXT("Reason_NoKey", "Requires a key");
	}
	if (ReasonTag == InteractNativeTags::Reason_TooFar)
	{
		return LOCTEXT("Reason_TooFar", "Too far away");
	}
	if (ReasonTag == InteractNativeTags::Reason_NotEnoughResource)
	{
		return LOCTEXT("Reason_NotEnoughResource", "Not enough resources");
	}
	if (ReasonTag == InteractNativeTags::Reason_OnCooldown)
	{
		return LOCTEXT("Reason_OnCooldown", "On cooldown");
	}
	if (ReasonTag == InteractNativeTags::Reason_Full)
	{
		return LOCTEXT("Reason_Full", "Full");
	}
	return LOCTEXT("Reason_Unavailable", "Unavailable");
}

TScriptInterface<ISeam_EntityIdentity> UInteract_AvailabilityHelper::ResolveInstigatorIdentity(AActor* InstigatorActor)
{
	TScriptInterface<ISeam_EntityIdentity> Result;
	if (!InstigatorActor)
	{
		return Result;
	}

	// The actor itself may carry the identity seam.
	if (InstigatorActor->Implements<USeam_EntityIdentity>())
	{
		Result.SetObject(InstigatorActor);
		Result.SetInterface(Cast<ISeam_EntityIdentity>(InstigatorActor));
		return Result;
	}

	// Otherwise the first component implementing it (the Entity module's identity component).
	for (UActorComponent* Component : InstigatorActor->GetComponents())
	{
		if (Component && Component->Implements<USeam_EntityIdentity>())
		{
			Result.SetObject(Component);
			Result.SetInterface(Cast<ISeam_EntityIdentity>(Component));
			return Result;
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
