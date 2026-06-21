// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsSkillTreeModule.h"
#include "Core/DPLog.h"

namespace SkillNativeTags
{
	// ---- Vocabulary roots ----
	UE_DEFINE_GAMEPLAY_TAG(Skill,         "Skill");
	UE_DEFINE_GAMEPLAY_TAG(Skill_Node,    "Skill.Node");
	UE_DEFINE_GAMEPLAY_TAG(Skill_Ability, "Skill.Ability");
	UE_DEFINE_GAMEPLAY_TAG(Skill_Channel, "Skill.Channel");
	UE_DEFINE_GAMEPLAY_TAG(Skill_Mutex,   "Skill.Mutex");
	UE_DEFINE_GAMEPLAY_TAG(Skill_Persist, "Skill.Persist");

	// ---- Message-bus channels (children of the core DP.Bus root) ----
	UE_DEFINE_GAMEPLAY_TAG(Bus_Skill,               "DP.Bus.Skill");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Skill_Learned,       "DP.Bus.Skill.Learned");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Skill_Unlearned,     "DP.Bus.Skill.Unlearned");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Skill_Respec,        "DP.Bus.Skill.Respec");
	UE_DEFINE_GAMEPLAY_TAG(Bus_Skill_PointsChanged, "DP.Bus.Skill.PointsChanged");

	// ---- Service-locator keys (children of the core DP.Service root) ----
	UE_DEFINE_GAMEPLAY_TAG(Service_Skill_AbilityGranter, "DP.Service.Skill.AbilityGranter");
	UE_DEFINE_GAMEPLAY_TAG(Service_Skill_PointSource,    "DP.Service.Skill.PointSource");
}

void FDesignPatternsSkillTreeModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSkillTree module started."));
}

void FDesignPatternsSkillTreeModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSkillTree module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsSkillTreeModule, DesignPatternsSkillTree)
