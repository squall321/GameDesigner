// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

/**
 * Logging categories and stat group for the DesignPatterns plugin.
 *
 * One log category per system so verbosity can be tuned independently
 * (e.g. `Log LogDPBus Verbose`). The umbrella `LogDP` is used for module
 * lifecycle and cross-cutting messages.
 */
DECLARE_LOG_CATEGORY_EXTERN(LogDP, Log, All);          // umbrella / module lifecycle
DECLARE_LOG_CATEGORY_EXTERN(LogDPBus, Log, All);       // message bus
DECLARE_LOG_CATEGORY_EXTERN(LogDPPool, Log, All);      // object pool
DECLARE_LOG_CATEGORY_EXTERN(LogDPFSM, Log, All);       // finite state machine
DECLARE_LOG_CATEGORY_EXTERN(LogDPCmd, Log, All);       // command pattern
DECLARE_LOG_CATEGORY_EXTERN(LogDPService, Log, All);   // service locator
DECLARE_LOG_CATEGORY_EXTERN(LogDPData, Log, All);      // data registry
DECLARE_LOG_CATEGORY_EXTERN(LogDPSave, Log, All);      // save system
DECLARE_LOG_CATEGORY_EXTERN(LogDPFactory, Log, All);   // spawn factory
DECLARE_LOG_CATEGORY_EXTERN(LogDPAction, Log, All);    // lightweight actions

/**
 * Shared stat group: view with `stat DesignPatterns`.
 * Individual counters are declared next to the system that owns them.
 */
DECLARE_STATS_GROUP(TEXT("DesignPatterns"), STATGROUP_DesignPatterns, STATCAT_Advanced);
