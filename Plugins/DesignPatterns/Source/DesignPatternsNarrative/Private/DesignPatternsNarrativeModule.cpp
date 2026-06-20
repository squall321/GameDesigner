// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

class FDesignPatternsNarrativeModule : public IModuleInterface
{
public:
	virtual void StartupModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsNarrative module started.")); }
	virtual void ShutdownModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsNarrative module shut down.")); }
};

IMPLEMENT_MODULE(FDesignPatternsNarrativeModule, DesignPatternsNarrative)
