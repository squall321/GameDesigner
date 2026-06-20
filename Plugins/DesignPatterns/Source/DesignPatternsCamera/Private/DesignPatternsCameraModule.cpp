// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

class FDesignPatternsCameraModule : public IModuleInterface
{
public:
	virtual void StartupModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsCamera module started.")); }
	virtual void ShutdownModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsCamera module shut down.")); }
};

IMPLEMENT_MODULE(FDesignPatternsCameraModule, DesignPatternsCamera)
