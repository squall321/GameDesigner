// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the story-director / sequence area of the Narrative module.
 *
 * Complements DesignPatternsNarrativeModule.h (which anchors the dialogue bus channels and speaker
 * roots). These anchor:
 *  - DP.Bus.Narrative.Story.*     story-beat / arc lifecycle bus channels (observer-only cosmetics).
 *  - DP.Bus.Narrative.Sequence.*  cutscene/sequence lifecycle bus channels.
 *  - DP.Service.Narrative.*       service-locator keys (the story director registers itself as the
 *                                 INarr_StoryConditionSource provider).
 *  - DP.Persist.Narrative.*       persistence-kind tags used by ISeam_Persistable participants.
 *  - DP.InputMode.Cutscene        input-mode tag pushed onto the arbiter while a cutscene plays.
 *
 * Concrete beat/arc keys (Narr.Beat.*, Narr.Arc.*) are authored by the project / its data assets, NOT
 * here. Full tag strings live in Narr_StoryNativeTags.cpp.
 */
namespace NarrativeStoryNativeTags
{
	// --- Story-beat / arc lifecycle bus channels (children of DP.Bus.Narrative) ---

	/** Bus channel: a story beat became active. Payload: FNarr_StoryEventPayload (beat + arc). */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_Story_BeatStarted);

	/** Bus channel: a story beat completed. Payload: FNarr_StoryEventPayload. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_Story_BeatCompleted);

	/** Bus channel: a beat was offered but blocked because its prerequisites failed. Payload: same. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_Story_BeatBlocked);

	/** Bus channel: a story arc became active (its first beat started). Payload: arc in BeatTag slot. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_Story_ArcStarted);

	/** Bus channel: a story arc completed (all its tracked beats done). Payload: arc in BeatTag slot. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_Story_ArcCompleted);

	// --- Cutscene / sequence lifecycle bus channels (children of DP.Bus.Narrative) ---

	/** Bus channel: a level-sequence cutscene started playing. Payload: FNarr_SequenceEventPayload. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_Sequence_Started);

	/** Bus channel: a cutscene finished naturally. Payload: FNarr_SequenceEventPayload. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_Sequence_Finished);

	/** Bus channel: a cutscene was skipped by the player. Payload: FNarr_SequenceEventPayload. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_Narrative_Sequence_Skipped);

	// --- Service-locator keys (children of the core DP.Service root) ---

	/** Service key under which the story director registers as the INarr_StoryConditionSource. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Narrative_ConditionSource);

	/** Service key under which the story director registers itself (for direct typed resolution). */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Narrative_StoryDirector);

	/** Service key under which UNarr_ReputationSubsystem registers as the ISeam_Reputation owner. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Narrative_Reputation);

	// --- Persistence-kind tags (children of a DP.Persist root) ---

	/** Persistence kind for the story director's beat/arc tracking record. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Narrative_Story);

	/** Persistence kind for the reputation subsystem's standing record. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Narrative_Reputation);

	// --- Input-mode tag pushed onto the shared input-mode arbiter while a cutscene plays ---

	/** Input-mode tag the sequence director pushes onto ISeam_InputModeArbiter during a cutscene. */
	DESIGNPATTERNSNARRATIVE_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(InputMode_Cutscene);
}
