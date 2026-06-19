// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * RPG gameplay module for the DesignPatterns plugin.
 *
 * Builds an item/inventory/stats/quest layer on top of the core Data registry and Save
 * systems. All authoritative gameplay state (inventory contents, equipment, level/XP,
 * quest progression) is server-authoritative and replicated to clients; mutators are
 * authority-guarded at the top of each function per the core's binding contract.
 *
 * Depends on the runtime core module (which hosts the data registry + save subsystem
 * this module's content and components build on).
 */
class FDesignPatternsRPGModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
