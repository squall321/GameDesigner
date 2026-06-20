// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

class FDesignPatternsInventoryUIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsInventoryUI module started.")); }
	virtual void ShutdownModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsInventoryUI module shut down.")); }
};

IMPLEMENT_MODULE(FDesignPatternsInventoryUIModule, DesignPatternsInventoryUI)
