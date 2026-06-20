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

	// Command identity: the optional undoable/replayable "perform interaction" command.
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cmd_Interact);

	// Data identity root for verb definitions: DP.Data.Interact.Verb (concrete verbs are children).
	DESIGNPATTERNSINTERACTION_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Interact_Verb);
}
