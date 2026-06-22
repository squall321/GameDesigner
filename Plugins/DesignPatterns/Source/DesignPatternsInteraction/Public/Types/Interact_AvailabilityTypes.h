// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakObjectPtr.h"
#include "Interact_AvailabilityTypes.generated.h"

class AActor;

/**
 * How a verb is activated by player input.
 *
 * The shipped UInteract_VerbDefinition expresses only instant (bHoldToActivate=false) vs hold
 * (bHoldToActivate=true). This enum (carried by the additive UInteract_VerbDefinitionEx subclass)
 * lets the input driver implement richer activation styles WITHOUT changing the shipped class.
 * The authoritative hold duration still comes from the base class's HoldSeconds so the server's
 * hold-completion math is unchanged; the extra modes only shape the LOCAL input gesture.
 */
UENUM(BlueprintType)
enum class EInteract_ActivationMode : uint8
{
	/** Activates instantly on press (maps to bHoldToActivate=false). */
	Tap,

	/** Requires holding the input for the verb's HoldSeconds (maps to bHoldToActivate=true). */
	Hold,

	/** Holds and accumulates a [0,1] charge over ChargeMaxSeconds; releasing fires with the charge level. */
	Charge,

	/** Two presses within DoubleTapWindowSeconds activate the verb (single tap is ignored). */
	DoubleTap,

	/** While held, re-fires every RepeatIntervalSeconds (auto-repeat, e.g. searching a stack). */
	Repeat
};

/**
 * Per-verb usability record assembled FROM the ISeam_InteractAvailability seam result by
 * UInteract_AvailabilityHelper.
 *
 * Lives entirely in the Interaction module and depends only on FGameplayTag / FText, so it never
 * crosses the leaf Seams interface. When the focused interactable does not implement the
 * availability seam, bEnabled stays true and ReasonTag/ReasonText stay empty.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_VerbAvailability
{
	GENERATED_BODY()

	/** The verb this record describes (a child of DP.Data.Interact.Verb). */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Availability", meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag Verb;

	/** Player-facing action label (e.g. "Open"), copied from the verb's prompt info. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Availability")
	FText ActionText;

	/** True when the verb is currently usable. When false the UI shows it greyed-out with ReasonText. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Availability")
	bool bEnabled = true;

	/** When disabled, the DP.Interact.Reason.* tag describing why (empty when enabled). */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Availability", meta = (Categories = "DP.Interact.Reason"))
	FGameplayTag ReasonTag;

	/** When disabled, the resolved player-facing reason text (from the seam or a per-reason fallback). */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Availability")
	FText ReasonText;

	/** Optional icon/style identity tag for the prompt UI (copied from the verb definition). */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Availability")
	FGameplayTag PromptStyleTag;

	FInteract_VerbAvailability() = default;
};

/**
 * The full multi-verb surface for a single focused target: every supported verb, each with its
 * usability/availability folded in, plus which one is the default selection.
 *
 * Built by combining IInteract_Interactable::GetSupportedVerbs (and the optional
 * IInteract_MultiVerbProvider) with the availability seam. Carries only value types + a weak actor
 * ref, so it is safe to hand to a ViewModel and never participates in replication.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_VerbMenu
{
	GENERATED_BODY()

	/** Every supported verb for the target, in supported order. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Availability")
	TArray<FInteract_VerbAvailability> Verbs;

	/** Index into Verbs of the default/highlighted selection, or INDEX_NONE when the menu is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Availability")
	int32 DefaultVerbIndex = INDEX_NONE;

	/** The target the menu describes. Non-owning. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Availability")
	TWeakObjectPtr<AActor> Target;

	FInteract_VerbMenu() = default;

	/** True when more than one ENABLED verb exists (i.e. a context menu is worth showing). */
	bool HasMultipleEnabled() const
	{
		int32 EnabledCount = 0;
		for (const FInteract_VerbAvailability& Entry : Verbs)
		{
			if (Entry.bEnabled && ++EnabledCount > 1)
			{
				return true;
			}
		}
		return false;
	}

	/** Number of verbs in the menu (enabled or not). */
	int32 Num() const { return Verbs.Num(); }

	/** True when there are no verbs at all. */
	bool IsEmpty() const { return Verbs.Num() == 0; }
};

/**
 * Bus payload for DP.Bus.Interact.Denied — broadcast (server side) when an interaction request was
 * re-validated and rejected because a verb was unavailable.
 *
 * A NEW payload type so the shipped FInteract_BusPayload is left 100% untouched. Value-only; the
 * broadcaster wraps it in an FInstancedStruct exactly like FInteract_BusPayload (never plain-replicated).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_DenialPayload
{
	GENERATED_BODY()

	/** The instigating actor whose request was denied. Non-owning. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Bus")
	TWeakObjectPtr<AActor> Instigator;

	/** The target actor the request named (server-resolved). Non-owning. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Bus")
	TWeakObjectPtr<AActor> Target;

	/** The verb that was requested. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Bus", meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag Verb;

	/** The DP.Interact.Reason.* tag describing why the request was denied. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Bus", meta = (Categories = "DP.Interact.Reason"))
	FGameplayTag ReasonTag;

	/** Server world time (seconds) at which the denial occurred. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Bus")
	double ServerTimeSeconds = 0.0;

	FInteract_DenialPayload() = default;
};

/**
 * Bus payload for DP.Bus.Interact.BatchComplete — broadcast (server side) after a batch
 * ("interact with all") request finishes, carrying how many targets were successfully interacted with.
 *
 * Value-only; wrapped in an FInstancedStruct by the broadcaster. Never plain-replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_BatchPayload
{
	GENERATED_BODY()

	/** The instigating actor that ran the batch. Non-owning. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Bus")
	TWeakObjectPtr<AActor> Instigator;

	/** The verb that was applied to each accepted target. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Bus", meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag Verb;

	/** How many targets the server accepted and ran BeginInteract on (after clamping to MaxBatchTargets). */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Bus")
	int32 SuccessCount = 0;

	/** Server world time (seconds) at which the batch completed. */
	UPROPERTY(BlueprintReadOnly, Category = "Interact|Bus")
	double ServerTimeSeconds = 0.0;

	FInteract_BatchPayload() = default;
};
