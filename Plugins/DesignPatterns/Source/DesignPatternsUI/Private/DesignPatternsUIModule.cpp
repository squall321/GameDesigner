// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsUIModule.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsUIModule"

void FDesignPatternsUIModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsUI module started."));
}

void FDesignPatternsUIModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsUI module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDesignPatternsUIModule, DesignPatternsUI)
