// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Types/Interact_Types.h"
#include "Interact_Interactable.generated.h"

UINTERFACE(BlueprintType, MinimalAPI, meta = (CannotImplementInterfaceInBlueprint = false))
class UInteract_Interactable : public UInterface
{
	GENERATED_BODY()
};

/**
 * The contract every interactable object (actor or component) implements so the interaction
 * system can target it without knowing its concrete type.
 *
 * AUTHORITY CONTRACT:
 *   - CanInteract / GetSupportedVerbs / GetInteractionPrompt are READ-ONLY and may run on any
 *     machine (clients call them for local focus + prompt display).
 *   - BeginInteract and EndInteract MUTATE state and are AUTHORITY-ONLY by contract: they are
 *     only ever invoked by UInteract_InteractorComponent on the server (after it re-derives and
 *     re-validates the target). Implementations must early-return / guard on
 *     GetOwner()->HasAuthority() and never assume client invocation. Clients learn the outcome
 *     through replication and the DP.Bus.Interact.* events, never by calling these directly.
 *
 * All methods are BlueprintNativeEvents so designers can author interactables in Blueprint while
 * C++ implementations override the _Implementation.
 */
class DESIGNPATTERNSINTERACTION_API IInteract_Interactable
{
	GENERATED_BODY()

public:
	/**
	 * Read-only eligibility test for the given query. Must be side-effect free (it runs on clients
	 * for focus highlighting and on the server before BeginInteract). Return false to be skipped
	 * entirely as a focus candidate.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interact")
	bool CanInteract(const FInteract_Query& Query) const;

	/**
	 * Append every verb this interactable supports (children of DP.Data.Interact.Verb) to OutVerbs.
	 * Read-only. The first appended verb is treated as the default when a query has no DesiredVerb.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interact")
	void GetSupportedVerbs(FGameplayTagContainer& OutVerbs) const;

	/**
	 * Produce the prompt to display for this interactable under the given query (title, action text,
	 * verb, enabled state). Read-only; runs locally on the client that is focusing the interactable.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interact")
	FInteract_PromptInfo GetInteractionPrompt(const FInteract_Query& Query) const;

	/**
	 * AUTHORITY ONLY. Begin the interaction described by Context. Called by the interactor component
	 * on the server after it re-derives + validates the target. Implementations must guard authority
	 * and may start a timer (for holds), spawn effects, change replicated state, etc.
	 * @return true if the interaction was accepted and started.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interact")
	bool BeginInteract(const FInteract_Context& Context);

	/**
	 * AUTHORITY ONLY. End an interaction previously started with BeginInteract. Called by the
	 * interactor component on the server (completion, cancel, range loss, interruption).
	 * Implementations must guard authority.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Interact")
	void EndInteract(const FInteract_Context& Context, EInteract_EndReason Reason);
};
