// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) tags for the DesignPatternsInteraction module.
 *
 * These anchor concrete interaction channels/commands/data UNDER the core roots
 * (DP.Bus, DP.Cmd, DP.Data) so tag-hierarchy matching keeps working — e.g. a listener on
 * DP.Bus or DP.Bus.Interact receives DP.Bus.Interact.Begin. The full tag strings are
 * defined in DesignPatternsInteractionModule.cpp.
 *
 * Bus channels carry an FInteract_BusPayload (see Types/Interact_Types.h) describing the
 * instigator, target and verb of the interaction event.
 */
namespace InteractNativeTags
{
	// Bus channel: broadcast on the server when an interaction has begun (BeginInteract succeeded).
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Interact_Begin);

	// Bus channel: broadcast on the server when an interaction completed normally.
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Interact_Complete);

	// Bus channel: broadcast on the server when an interaction was cancelled / interrupted.
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Interact_Cancel);

	// Bus channel: broadcast on the server when an interaction request was denied (verb unavailable).
	// Carries an FInteract_DenialPayload (see Types/Interact_AvailabilityTypes.h).
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Interact_Denied);

	// Bus channel: broadcast on the server when a batch ("interact with all") request completed.
	// Carries an FInteract_BatchPayload.
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Interact_BatchComplete);

	// Command identity: the optional undoable/replayable "perform interaction" command.
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cmd_Interact);

	// Command identity: the multi-target "interact with all" batch command.
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cmd_BatchInteract);

	// Data identity root for verb definitions: DP.Data.Interact.Verb (concrete verbs are children).
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Interact_Verb);

	// ---- Availability reasons (DP.Interact.Reason.*) ----
	// Carried across the ISeam_InteractAvailability seam (which uses only FGameplayTag for the reason)
	// and surfaced in prompts / on DP.Bus.Interact.Denied. Anchored under a DP.Interact.Reason root so
	// hierarchy matching works for project-defined sub-reasons.

	// Generic "the reason root" — a project may match the whole family.
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_Locked);
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_NoKey);
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_TooFar);
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_NotEnoughResource);
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_OnCooldown);
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Reason_Full);
}
