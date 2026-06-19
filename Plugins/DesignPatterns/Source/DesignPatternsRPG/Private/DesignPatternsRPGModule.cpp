// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsRPGModule.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsRPGModule"

void FDesignPatternsRPGModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsRPG module started."));
}

void FDesignPatternsRPGModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsRPG module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDesignPatternsRPGModule, DesignPatternsRPG)
