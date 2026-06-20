// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsHUDModule.h"
#include "Modules/ModuleManager.h"
#include "Core/DPLog.h"

void FDesignPatternsHUDModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsHUD module started."));
}

void FDesignPatternsHUDModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsHUD module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsHUDModule, DesignPatternsHUD)
