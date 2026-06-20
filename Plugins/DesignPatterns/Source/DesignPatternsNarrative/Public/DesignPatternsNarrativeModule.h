// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsNarrative module.
 *
 * Two families are anchored here so tag-hierarchy matching always works at startup
 * (a listener on DP.Bus or DP.Bus.Narrative receives DP.Bus.Narrative.LineShown):
 *
 *  - DP.Bus.Narrative.*   message-bus channels the dialogue runner broadcasts on as
 *                         OBSERVER-ONLY notifications (UI/audio/analytics may listen; these
 *                         events NEVER drive dialogue flow — the presenter seam drives flow).
 *  - Narr.Speaker.*       roots under which a project authors concrete speaker identity tags
 *                         (e.g. Narr.Speaker.Npc.Merchant). Only the roots are anchored here;
 *                         concrete speaker tags are authored in the project tag table.
 *
 * Concrete dialogue/condition keys (graph node ids, hub flag keys) are authored by the game
 * project and its data assets, NOT here. Full tag strings live in Narrative_NativeTags.cpp.
 */
namespace NarrativeNativeTags
{
	// --- Message-bus channels (children of the core DP.Bus root) ---

	/** Bus channel: broadcast when a dialogue runner begins running a graph (payload: graph + speaker). */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_DialogueStarted);

	/** Bus channel: broadcast when a dialogue runner presents a line (payload: speaker + node id). */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_LineShown);

	/** Bus channel: broadcast when a dialogue runner presents a choice set (payload: node id + count). */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_ChoicesShown);

	/** Bus channel: broadcast when a choice is committed (payload: node id + choice id). */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_ChoiceSelected);

	/** Bus channel: broadcast when a dialogue runner finishes / aborts a graph (payload: graph + reason). */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_DialogueFinished);

	/** Bus channel: arbitrary designer-authored story event raised by a graph "event" node / effect. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_StoryEvent);

	// --- Speaker identity roots (authored leaves live in the project tag table) ---

	/** Root anchor for all speaker identity tags (Narr.Speaker.*). */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Speaker_Root);

	/** Conventional speaker tag for the local player / narrator voice. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Speaker_Player);

	/** Conventional speaker tag for an unattributed narrator / system voice. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Speaker_Narrator);

	// --- Input-mode tag pushed onto the shared input-mode arbiter while dialogue is active ---

	/** Input-mode tag the runner pushes onto ISeam_InputModeArbiter while a dialogue is on screen. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode_Dialogue);
}
