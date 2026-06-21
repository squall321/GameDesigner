// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Story/Narr_StoryNativeTags.h"

namespace NarrativeStoryNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_Story_BeatStarted, "DP.Bus.Narrative.Story.BeatStarted",
		"Observer-only: a story beat became active. Payload carries the beat and arc tags.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_Story_BeatCompleted, "DP.Bus.Narrative.Story.BeatCompleted",
		"Observer-only: a story beat completed. Payload carries the beat and arc tags.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_Story_BeatBlocked, "DP.Bus.Narrative.Story.BeatBlocked",
		"Observer-only: a beat was requested but blocked by failing prerequisites. Payload carries the beat tag.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_Story_ArcStarted, "DP.Bus.Narrative.Story.ArcStarted",
		"Observer-only: a story arc became active (its first beat started). Payload carries the arc tag.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_Story_ArcCompleted, "DP.Bus.Narrative.Story.ArcCompleted",
		"Observer-only: a story arc completed (all tracked beats done). Payload carries the arc tag.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_Sequence_Started, "DP.Bus.Narrative.Sequence.Started",
		"Observer-only: a level-sequence cutscene started. Payload carries the sequence tag and instigator.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_Sequence_Finished, "DP.Bus.Narrative.Sequence.Finished",
		"Observer-only: a cutscene finished naturally. Payload carries the sequence tag.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_Narrative_Sequence_Skipped, "DP.Bus.Narrative.Sequence.Skipped",
		"Observer-only: a cutscene was skipped by the player. Payload carries the sequence tag.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_Narrative_ConditionSource, "DP.Service.Narrative.ConditionSource",
		"Service key under which the story director registers as the INarr_StoryConditionSource.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_Narrative_StoryDirector, "DP.Service.Narrative.StoryDirector",
		"Service key under which the story director registers itself for direct typed resolution.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_Narrative_Reputation, "DP.Service.Narrative.Reputation",
		"Service key under which UNarr_ReputationSubsystem registers as the ISeam_Reputation owner.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Persist_Narrative_Story, "DP.Persist.Narrative.Story",
		"Persistence-kind tag for the story director's beat/arc tracking record.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Persist_Narrative_Reputation, "DP.Persist.Narrative.Reputation",
		"Persistence-kind tag for the reputation subsystem's standing record.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(InputMode_Cutscene, "DP.InputMode.Cutscene",
		"Input-mode tag the sequence director pushes onto ISeam_InputModeArbiter while a cutscene plays.");
}
