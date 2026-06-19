// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsPlatformModule.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsPlatformModule"

void FDesignPatternsPlatformModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsPlatform module started."));
}

void FDesignPatternsPlatformModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsPlatform module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDesignPatternsPlatformModule, DesignPatternsPlatform)
