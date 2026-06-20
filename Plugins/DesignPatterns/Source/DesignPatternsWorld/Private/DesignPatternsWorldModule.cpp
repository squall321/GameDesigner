// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsWorldModule.h"
#include "Core/DPLog.h"

void FDesignPatternsWorldModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsWorld module started."));
}

void FDesignPatternsWorldModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsWorld module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsWorldModule, DesignPatternsWorld)
