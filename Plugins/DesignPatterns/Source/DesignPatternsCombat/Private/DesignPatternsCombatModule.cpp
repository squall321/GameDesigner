// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

/**
 * Runtime Action/Combat module for the DesignPatterns plugin.
 *
 * Builds on the core FSM + Action systems to provide networked health, hitbox-based
 * hit detection, an input-window combo graph and timed status effects. Reuses the
 * core message bus (resolved via FDP_SubsystemStatics) to broadcast combat events.
 */
class FDesignPatternsCombatModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDP, Log, TEXT("DesignPatternsCombat module started."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDP, Log, TEXT("DesignPatternsCombat module shut down."));
	}
};

IMPLEMENT_MODULE(FDesignPatternsCombatModule, DesignPatternsCombat)
