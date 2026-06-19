// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Cross-platform abstraction module for the DesignPatterns plugin.
 *
 * Provides a thin, platform-agnostic facade over input source detection, device
 * capability / performance tiering, per-platform save-path resolution and application
 * lifecycle (suspend/resume) so the rest of the plugin and the host game never need to
 * write #if PLATFORM_* branches. All platform branching is confined to this module.
 *
 * Depends on the runtime core module (subsystem bases, logging, native tag roots).
 */
class FDesignPatternsPlatformModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
