// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

/**
 * Opt-in GAS bridge module. The only module that links GameplayAbilities.
 * Provides UDP_GASBridgeComponent to forward DesignPatterns lightweight actions
 * into a real AbilitySystemComponent for projects that use GAS.
 */
class FDesignPatternsGASModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDPAction, Log, TEXT("DesignPatternsGAS bridge module started."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDPAction, Log, TEXT("DesignPatternsGAS bridge module shut down."));
	}
};

IMPLEMENT_MODULE(FDesignPatternsGASModule, DesignPatternsGAS)
