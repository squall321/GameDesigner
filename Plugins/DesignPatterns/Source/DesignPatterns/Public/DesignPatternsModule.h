// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Runtime core module for the DesignPatterns plugin.
 *
 * Hosts the subsystem bases, message bus, object pool, service locator, finite state
 * machine, command/strategy/factory/action patterns, data registry and save system.
 * This is the single mandatory module that every other plugin module depends on.
 */
class FDesignPatternsModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface

	/** Convenience accessor for the loaded module. */
	static FDesignPatternsModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FDesignPatternsModule>("DesignPatterns");
	}

	/** Returns true if the module is currently loaded. */
	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("DesignPatterns");
	}
};
