// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Survival/crafting runtime module for the DesignPatterns plugin.
 *
 * Builds on top of the core "DesignPatterns" module (data registry, subsystem bases, message
 * bus, native tags, logging). Provides harvestable resource nodes, a queue-based crafting
 * system, survival needs (hunger/thirst/stamina/temperature), a day/night time-of-day clock
 * and tool durability. All gameplay-state mutation is authority-guarded and replicated where
 * it must be visible to clients.
 */
class FDesignPatternsSurvivalModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
