// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

class FDesignPatternsModContentEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsModContentEditor module started.")); }
	virtual void ShutdownModule() override { UE_LOG(LogDP, Log, TEXT("DesignPatternsModContentEditor module shut down.")); }
};

IMPLEMENT_MODULE(FDesignPatternsModContentEditorModule, DesignPatternsModContentEditor)
