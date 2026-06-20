// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

class FDesignPatternsSimGridModule : public IModuleInterface
{
public:
	virtual void StartupModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsSimGrid module started.")); }
	virtual void ShutdownModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsSimGrid module shut down.")); }
};

IMPLEMENT_MODULE(FDesignPatternsSimGridModule, DesignPatternsSimGrid)
