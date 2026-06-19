// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

/**
 * Developer module: registers the DP.* console commands and stat hooks.
 * All command bodies live in DPConsoleCommands.cpp wrapped in `#if !UE_BUILD_SHIPPING`.
 */
class FDesignPatternsDeveloperModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		UE_LOG(LogDP, Log, TEXT("DesignPatternsDeveloper module started (console commands registered)."));
	}

	virtual void ShutdownModule() override
	{
		UE_LOG(LogDP, Log, TEXT("DesignPatternsDeveloper module shut down."));
	}
};

IMPLEMENT_MODULE(FDesignPatternsDeveloperModule, DesignPatternsDeveloper)
