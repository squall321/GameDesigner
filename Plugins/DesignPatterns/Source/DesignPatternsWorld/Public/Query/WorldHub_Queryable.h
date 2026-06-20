// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "WorldHub_Queryable.generated.h"

/**
 * Read-only seam onto world-hub state.
 *
 * Implemented by the hub subsystem (and any adapter that wants to expose a hub-shaped read view).
 * Consumers depend on THIS interface, never on the concrete subsystem class, so other modules read
 * world state through a TScriptInterface resolved from the service locator. The interface is pure
 * read: there are no mutators here (writes go through the player-owned intent component and the
 * server-side authority API on the subsystem).
 */
UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UWorldHub_Queryable : public UInterface
{
	GENERATED_BODY()
};

/** @see UWorldHub_Queryable */
class DESIGNPATTERNSWORLD_API IWorldHub_Queryable
{
	GENERATED_BODY()

public:
	/**
	 * Read the effective value of a flag for a scope into Out (as an FInstancedStruct).
	 *
	 * Implementations should apply scope fallback (Entity/Faction -> Global) when the exact scope
	 * has no value, and should fill Out from the flag definition's default when nothing is stored.
	 *
	 * @param Key   the flag identity to read.
	 * @param Scope the scope to read for.
	 * @param Out   receives the value on success; left untouched on failure.
	 * @return true if a value (stored or default) was produced.
	 */
	virtual bool QueryValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope, FInstancedStruct& Out) const = 0;

	/**
	 * @return true if a concrete (stored, non-default) value exists for Key in Scope after applying
	 * fallback. Implementations must NOT count a definition default as "has value".
	 */
	virtual bool HasValue(const FGameplayTag& Key, const FWorldHub_Scope& Scope) const = 0;
};
