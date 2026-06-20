// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * World data-hub module for the DesignPatterns plugin.
 *
 * Hosts the central query/mutation spine for game-wide state: scoped flags, variables and
 * counters keyed by FGameplayTag and resolved through a small set of scopes (global, faction,
 * entity), plus scoped blackboards that bridge the core UDP_Blackboard into the hub's change
 * pipeline. State is save-backed and OPTIONALLY replicated: subsystems hold the server-side
 * authority API but never replicate; authoritative replicated mirrors live on
 * components/AInfo carriers (authored by the subsystem area of this module).
 *
 * This area provides the foundational, dependency-free building blocks of the module:
 *  - Native anchor tags (children of the core DP.Service / DP.Bus roots).
 *  - The FWorldHub_Scope addressing key (no weak pointers; net- and save-stable).
 *  - The flag value / definition / replicated-entry value types.
 *  - The UWorldHub_FlagSetDataAsset that authors flag definitions as data.
 *  - The IWorldHub_Queryable / IWorldHub_StateProvider read seams.
 *  - The UWorldHub_ScopedBlackboard adapter onto the core blackboard provider seam.
 *
 * Depends only on the runtime core module and the Seams module — never on a genre/high-level
 * module's concrete components (cross-module coupling is through Seams only).
 */
class FDesignPatternsWorldModule : public IModuleInterface
{
public:
	//~ Begin IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	//~ End IModuleInterface
};
