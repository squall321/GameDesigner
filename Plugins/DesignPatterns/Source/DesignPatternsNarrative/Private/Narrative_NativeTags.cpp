// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsNarrativeModule.h"

namespace NarrativeNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_DialogueStarted, "DP.Bus.Narrative.DialogueStarted",
		"Observer-only: a dialogue runner began running a graph. Payload carries the graph tag and root speaker.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_LineShown, "DP.Bus.Narrative.LineShown",
		"Observer-only: a dialogue line was presented. Payload carries the speaker tag and node id.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_ChoicesShown, "DP.Bus.Narrative.ChoicesShown",
		"Observer-only: a choice set was presented. Payload carries the node id and choice count.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_ChoiceSelected, "DP.Bus.Narrative.ChoiceSelected",
		"Observer-only: a choice was committed. Payload carries the node id and the selected choice id.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_DialogueFinished, "DP.Bus.Narrative.DialogueFinished",
		"Observer-only: a dialogue runner finished or aborted a graph. Payload carries the graph tag and reason.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_StoryEvent, "DP.Bus.Narrative.StoryEvent",
		"Designer-authored story event raised from a graph event node or a _BroadcastBusEvent effect.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Speaker_Root, "Narr.Speaker",
		"Root anchor for all speaker identity tags (Narr.Speaker.*).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Speaker_Player, "Narr.Speaker.Player",
		"Conventional speaker tag for the local player / player-character voice.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Speaker_Narrator, "Narr.Speaker.Narrator",
		"Conventional speaker tag for an unattributed narrator / system voice.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputMode_Dialogue, "DP.InputMode.Dialogue",
		"Input-mode tag the dialogue runner pushes onto ISeam_InputModeArbiter while dialogue is on screen.");
}
