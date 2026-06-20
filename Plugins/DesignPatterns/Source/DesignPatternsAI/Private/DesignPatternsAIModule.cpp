// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsAIModule.h"
#include "Core/DPLog.h"

void FDesignPatternsAIModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsAI module started."));
}

void FDesignPatternsAIModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsAI module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsAIModule, DesignPatternsAI)
