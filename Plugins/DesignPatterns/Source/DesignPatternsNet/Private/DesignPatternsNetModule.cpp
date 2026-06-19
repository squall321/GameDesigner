// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsNetModule.h"
#include "Core/DPLog.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsNetModule"

void FDesignPatternsNetModule::StartupModule()
{
#if WITH_DP_ONLINE
	UE_LOG(LogDP, Log, TEXT("DesignPatternsNet module started (online subsystem support: enabled)."));
#else
	UE_LOG(LogDP, Log, TEXT("DesignPatternsNet module started (online subsystem support: stubbed/unavailable)."));
#endif
}

void FDesignPatternsNetModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatternsNet module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDesignPatternsNetModule, DesignPatternsNet)
