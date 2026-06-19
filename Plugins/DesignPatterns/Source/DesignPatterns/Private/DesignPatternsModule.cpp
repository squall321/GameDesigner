// Copyright DesignPatterns plugin. All Rights Reserved.

#include "DesignPatternsModule.h"
#include "Core/DPLog.h"
#include "Save/DPSaveVersion.h"

#define LOCTEXT_NAMESPACE "FDesignPatternsModule"

void FDesignPatternsModule::StartupModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatterns runtime module started."));

	// Native gameplay tags are registered automatically by the FNativeGameplayTag
	// instances declared in DesignPatternsNativeTags.cpp (no manual call needed).

	// Register the save-format custom version up front so header-reading tools and the save
	// subsystem all see a consistent latest version, independent of subsystem init order.
	FDP_SaveVersion::Register();
}

void FDesignPatternsModule::ShutdownModule()
{
	UE_LOG(LogDP, Log, TEXT("DesignPatterns runtime module shut down."));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FDesignPatternsModule, DesignPatterns)
