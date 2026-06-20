// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Seams module: shared cross-cutting interface "seams" and value types that let the high-level
 * modules (Entity, World, SimGrid, Interaction, SimEconomy, SimAgents, InventoryUI) and the genre
 * modules compose without hard-depending on one another. Contains interfaces and POD/USTRUCT value
 * types only — no subsystems or components.
 */
class FDesignPatternsSeamsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
