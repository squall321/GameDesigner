// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsAINativeTags.h"

namespace AINativeTags
{
	// Message-bus channels. Anchored as children of the core DP.Bus root so the core message bus
	// hierarchy matching delivers them to broad listeners (e.g. one on DP.Bus or DP.Bus.AI).
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI,                     "DP.Bus.AI");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_Wave_Started,        "DP.Bus.AI.Wave.Started");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_Wave_Completed,      "DP.Bus.AI.Wave.Completed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_Wave_Cleared,        "DP.Bus.AI.Wave.Cleared");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_Encounter_Activated, "DP.Bus.AI.Encounter.Activated");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_Encounter_Completed, "DP.Bus.AI.Encounter.Completed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_Squad_Formed,        "DP.Bus.AI.Squad.Formed");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_Squad_Dissolved,     "DP.Bus.AI.Squad.Dissolved");

	// Service-locator keys, anchored under the core DP.Service root.
	UE_DEFINE_GAMEPLAY_TAG(Service_AI_Squad,         "DP.Service.AI.Squad");
	UE_DEFINE_GAMEPLAY_TAG(Service_AI_SpawnDirector, "DP.Service.AI.SpawnDirector");
	UE_DEFINE_GAMEPLAY_TAG(Service_AI_SpawnRegions,  "DP.Service.AI.SpawnRegions");

	// Perception / Behavior / Threat area: leaf bus channels (children of DP.Bus.AI) + vocabulary roots.
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_DecisionChanged, "DP.Bus.AI.DecisionChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_PerceptUpdated,  "DP.Bus.AI.PerceptUpdated");
	UE_DEFINE_GAMEPLAY_TAG(Bus_AI_ThreatChanged,   "DP.Bus.AI.ThreatChanged");

	UE_DEFINE_GAMEPLAY_TAG(AI_Percept,         "AI.Percept");
	UE_DEFINE_GAMEPLAY_TAG(AI_Percept_Sight,   "AI.Percept.Sight");
	UE_DEFINE_GAMEPLAY_TAG(AI_Percept_Hearing, "AI.Percept.Hearing");
	UE_DEFINE_GAMEPLAY_TAG(AI_Percept_Damage,  "AI.Percept.Damage");

	UE_DEFINE_GAMEPLAY_TAG(AI_Role, "AI.Role");
}
