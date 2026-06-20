// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Simulation-agents runtime module for the DesignPatterns plugin.
 *
 * Builds on the core "DesignPatterns" module (subsystem bases, service locator, data registry,
 * message bus, strategy selector, logging, native tags) and the shared "DesignPatternsSeams"
 * contracts (ISeam_SimClock, ISeam_NeedProvider). It provides:
 *   - a world simulation clock (USimAg_ClockSubsystem) that IMPLEMENTS the shared ISeam_SimClock
 *     seam and can either own authoritative time or derive it from an external time source
 *     (ISimAg_TimeSource — e.g. a Survival day/night clock adapter);
 *   - tag-keyed agent schedules (data asset + component) that read the clock on hour edges;
 *   - a generalized, replicated needs component implementing ISeam_NeedProvider.
 *
 * Survival / SimGrid / SimEconomy are reached ONLY through seams (no hard module dependency), so
 * this module composes cleanly with any genre layer or none at all.
 */
class FDesignPatternsSimAgentsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
