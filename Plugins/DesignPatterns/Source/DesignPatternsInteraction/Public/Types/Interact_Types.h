// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/EngineTypes.h"        // ECollisionChannel
#include "UObject/WeakObjectPtr.h"
#include "Interact_Types.generated.h"

class AActor;

/**
 * Why an interaction ended. Passed to IInteract_Interactable::EndInteract so the interactable
 * can react differently to a clean finish versus an interruption.
 */
UENUM(BlueprintType)
enum class EInteract_EndReason : uint8
{
	/** The interaction ran to completion (instant verbs, or a hold that reached its duration). */
	Completed,

	/** The instigator explicitly cancelled (released a hold early, pressed cancel). */
	Cancelled,

	/** The instigator moved out of range / lost line of sight while interacting. */
	OutOfRange,

	/** The interactable became unavailable (destroyed, disabled, consumed). */
	Interrupted
};

/**
 * Result of a (re-derived, server-side) interaction request. Sent back to the owning client as
 * lightweight feedback so the UI can show success/denial without trusting client prediction.
 */
UENUM(BlueprintType)
enum class EInteract_Result : uint8
{
	/** The request succeeded and the interaction began (or completed for instant verbs). */
	Success,

	/** No valid interactable was found by the server when it re-derived the target. */
	NoTarget,

	/** A target was found but its CanInteract() rejected the query. */
	Rejected,

	/** The target does not support the requested verb. */
	UnsupportedVerb,

	/** The request failed authority/validation (e.g. not the owning client, stale state). */
	NotAllowed,

	/** The instigator was out of range / line of sight when the server re-checked. */
	OutOfRange
};

/**
 * A point-in-time description of an interaction attempt, handed to the interactable so it can
 * decide whether it can be interacted with and what prompt to show. Built locally for focus
 * detection and rebuilt authoritatively on the server before BeginInteract.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_Query
{
	GENERATED_BODY()

	/** The actor attempting the interaction (the player pawn / controller's pawn). Non-owning. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	TWeakObjectPtr<AActor> Instigator;

	/** World-space eye/camera location the query is cast from. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	FVector ViewLocation = FVector::ZeroVector;

	/** World-space normalized look direction the query is cast along. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	FVector ViewDirection = FVector::ForwardVector;

	/** The verb the instigator wants to perform; empty means "the interactable's default verb". */
	UPROPERTY(BlueprintReadWrite, Category = "Interact", meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag DesiredVerb;

	FInteract_Query() = default;
};

/**
 * The authoritative context for an in-progress interaction, created on the server when
 * BeginInteract succeeds and handed to BeginInteract / EndInteract. Carries the server time the
 * interaction started so holds and timeouts are measured against a single authoritative clock.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_Context
{
	GENERATED_BODY()

	/** The interacting actor (server-resolved). Non-owning. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	TWeakObjectPtr<AActor> Instigator;

	/** The verb being performed (always resolved to a concrete tag by the server). */
	UPROPERTY(BlueprintReadWrite, Category = "Interact", meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag Verb;

	/** Server world time (seconds) at which the interaction began. 0 until stamped. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	double StartServerTimeSeconds = 0.0;

	FInteract_Context() = default;
};

/**
 * Everything the local UI needs to render an interaction prompt for the currently-focused
 * interactable. Produced by IInteract_Interactable::GetInteractionPrompt on the client.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_PromptInfo
{
	GENERATED_BODY()

	/** Headline text, e.g. "Wooden Door" or "Treasure Chest". */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	FText Title;

	/** Verb/action text, e.g. "Open" or "Pick Up". */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	FText Action;

	/** The verb this prompt corresponds to. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact", meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag Verb;

	/** When false the prompt is shown greyed-out / non-actionable (e.g. locked door). */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	bool bEnabled = true;

	FInteract_PromptInfo() = default;
};

/**
 * One detected interactable in the candidate set the focus strategy chooses from.
 *
 * Holds the focusable actor plus cheap geometric metadata (distance, angular offset, line-of-sight)
 * computed once during detection, so focus strategies can score candidates without re-tracing.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_Candidate
{
	GENERATED_BODY()

	/** The candidate actor; it (or one of its components) implements IInteract_Interactable. Non-owning. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	TWeakObjectPtr<AActor> Actor;

	/** The object that actually implements IInteract_Interactable (the actor or a component). Non-owning. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	TWeakObjectPtr<UObject> InteractableObject;

	/** World-space focus point used for distance/angle scoring (usually the actor's interaction socket). */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	FVector FocusLocation = FVector::ZeroVector;

	/** Distance (cm) from the query's ViewLocation to FocusLocation. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	float Distance = 0.f;

	/** Angle (degrees) between the query's ViewDirection and the direction to FocusLocation. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	float AngleDeg = 0.f;

	/** True if an unobstructed line of sight from ViewLocation to FocusLocation was confirmed. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	bool bHasLineOfSight = false;

	/** Designer-authored selection priority pulled from the interactable (higher wins ties). */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	int32 Priority = 0;

	FInteract_Candidate() = default;

	/** True if this candidate still references a live actor and interactable object. */
	bool IsValidCandidate() const
	{
		return Actor.IsValid() && InteractableObject.IsValid();
	}
};

/**
 * Tunable parameters controlling how the interactor component detects interactables.
 *
 * All values are designer-facing tunables (no magic constants in code). The component runs an
 * overlap/trace bounded by Range, narrows by ConeHalfAngleDeg, filters by Channel and optionally
 * requires line of sight before a candidate is eligible.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_DetectionParams
{
	GENERATED_BODY()

	/** Maximum interaction reach (cm) from the view location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Detection", meta = (ClampMin = "0.0", Units = "cm"))
	float Range = 250.f;

	/** Half-angle (degrees) of the acceptance cone around the view direction. 180 = full sphere. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Detection", meta = (ClampMin = "0.0", ClampMax = "180.0", Units = "deg"))
	float ConeHalfAngleDeg = 45.f;

	/** Collision channel used for the detection overlap and the line-of-sight trace. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Detection")
	TEnumAsByte<ECollisionChannel> Channel = ECollisionChannel::ECC_Visibility;

	/** When true a candidate is only eligible if an unobstructed line of sight to it exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Interact|Detection")
	bool bRequireLineOfSight = true;

	FInteract_DetectionParams() = default;
};

/**
 * Bus payload broadcast on DP.Bus.Interact.{Begin,Complete,Cancel}.
 *
 * Carries only replicated-safe value types (tags/guids resolved by name), so any listener — UI,
 * analytics, audio — can react to interaction lifecycle events without coupling to the component.
 * This is wrapped in an FInstancedStruct by the broadcaster (per the message-bus contract).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINTERACTION_API FInteract_BusPayload
{
	GENERATED_BODY()

	/** The instigating actor (may be null on listeners that receive it cross-frame). Non-owning. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	TWeakObjectPtr<AActor> Instigator;

	/** The interactable target actor. Non-owning. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	TWeakObjectPtr<AActor> Target;

	/** The verb that was performed. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact", meta = (Categories = "DP.Data.Interact.Verb"))
	FGameplayTag Verb;

	/** For Complete/Cancel events, why the interaction ended. Ignored for Begin. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	EInteract_EndReason EndReason = EInteract_EndReason::Completed;

	/** Server world time (seconds) at which the event occurred. */
	UPROPERTY(BlueprintReadWrite, Category = "Interact")
	double ServerTimeSeconds = 0.0;

	FInteract_BusPayload() = default;
};
