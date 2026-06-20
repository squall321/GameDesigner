// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

// Aggregation header for the world-hub value/scope/query types authored by this area.
//
// Sibling areas (the state-hub subsystem, the flag registry and the replication carrier) include
// "Types/WorldHub_Types.h" as a single convenience entry point for the shared types this area
// declares. It simply re-exports the real headers so there is exactly one definition of each type:
//
//   - FWorldHub_Scope                 (Hub/WorldHub_Scope.h)
//   - EWorldHub_FlagValueType,
//     FWorldHub_FlagValue,
//     FWorldHub_FlagDefinition,
//     FWorldHub_RepStateEntry          (Registry/WorldHub_FlagTypes.h)
//   - IWorldHub_Queryable             (Query/WorldHub_Queryable.h)
//   - IWorldHub_StateProvider         (Query/WorldHub_StateProvider.h)
//
// Keeping this as a pure re-export (no new types) means the value/scope/query contracts have a
// single source of truth in their dedicated headers.

#include "Hub/WorldHub_Scope.h"
#include "Registry/WorldHub_FlagTypes.h"
#include "Query/WorldHub_Queryable.h"
#include "Query/WorldHub_StateProvider.h"
