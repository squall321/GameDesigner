// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

class FDesignPatternsSaveSystemUIModule : public IModuleInterface
{
public:
	virtual void StartupModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsSaveSystemUI module started.")); }
	virtual void ShutdownModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsSaveSystemUI module shut down.")); }
};

IMPLEMENT_MODULE(FDesignPatternsSaveSystemUIModule, DesignPatternsSaveSystemUI)
