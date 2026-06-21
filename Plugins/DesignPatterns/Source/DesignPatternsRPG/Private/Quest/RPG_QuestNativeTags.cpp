// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Quest/RPG_QuestNativeTags.h"

namespace RPG_QuestNativeTags
{
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Quest_Activated,          "DP.Bus.RPG.Quest.Activated");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Quest_StageAdvanced,      "DP.Bus.RPG.Quest.StageAdvanced");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Quest_ObjectiveProgress,  "DP.Bus.RPG.Quest.ObjectiveProgress");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Quest_Completed,          "DP.Bus.RPG.Quest.Completed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Quest_Failed,             "DP.Bus.RPG.Quest.Failed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Journal_LoreUnlocked,     "DP.Bus.RPG.Journal.LoreUnlocked");

	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Objective_KillRoot,       "DP.Bus.RPG.Objective.Kill");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Objective_ReachRoot,      "DP.Bus.RPG.Objective.Reach");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Objective_EscortRoot,     "DP.Bus.RPG.Objective.Escort");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_Objective_DefendRoot,     "DP.Bus.RPG.Objective.Defend");

	UE_DEFINE_GAMEPLAY_TAG(WorldHub_RPG_StageRoot,           "DP.WorldHub.RPG.Stage");
	UE_DEFINE_GAMEPLAY_TAG(WorldHub_RPG_LoreRoot,            "DP.WorldHub.RPG.Lore");

	UE_DEFINE_GAMEPLAY_TAG(Service_RPG_QuestGraph,           "DP.Service.RPG.QuestGraph");
	UE_DEFINE_GAMEPLAY_TAG(Persist_RPG_QuestGraph,           "DP.Persist.RPG.QuestGraph");
}
