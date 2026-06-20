// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "WorldHub_StateProvider.generated.h"

/**
 * Seam for objects that DERIVE world-hub values on demand rather than storing them.
 *
 * Where IWorldHub_Queryable reads stored flag state, a state provider computes values live (e.g. a
 * clock adapter that provides "time of day" or a faction system that provides a reputation value).
 * The hub aggregates registered providers and consults them for keys it does not store itself, so
 * computed and stored state share one query surface without the hub hard-depending on the systems
 * that compute them.
 *
 * Implementers register through the service locator under DP.Service.WorldHub.Provider (or are
 * discovered off an owning actor) and advertise both their keys and the scope tag they apply to.
 */
UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UWorldHub_StateProvider : public UInterface
{
	GENERATED_BODY()
};

/** @see UWorldHub_StateProvider */
class DESIGNPATTERNSWORLD_API IWorldHub_StateProvider
{
	GENERATED_BODY()

public:
	/**
	 * Append every key this provider can answer to OutKeys.
	 *
	 * Called by the hub at registration (and on demand) to build a key->provider index so a query
	 * dispatches directly to the right provider without polling all of them.
	 */
	virtual void CollectProvidedKeys(TArray<FGameplayTag>& OutKeys) const = 0;

	/**
	 * Compute the current value for Key into Out.
	 *
	 * @param Key the key to compute; will be one previously returned from CollectProvidedKeys.
	 * @param Out receives the computed value on success; left untouched on failure.
	 * @return true if this provider produced a value for Key.
	 */
	virtual bool ProvideValue(const FGameplayTag& Key, FInstancedStruct& Out) const = 0;

	/**
	 * @return the scope tag this provider's values apply to (e.g. a faction tag, or an empty tag
	 * for global/session-wide values). The hub uses this to gate provider lookups to matching
	 * scopes so an entity-scoped query does not accidentally pull a different faction's computed
	 * value.
	 */
	virtual FGameplayTag GetProviderScopeTag() const = 0;
};
