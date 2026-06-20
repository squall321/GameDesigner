// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsSeamsModule.h"
#include "Core/DPLog.h"

void FDesignPatternsSeamsModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSeams module started."));
}

void FDesignPatternsSeamsModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsSeams module shut down."));
}

IMPLEMENT_MODULE(FDesignPatternsSeamsModule, DesignPatternsSeams)
