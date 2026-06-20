// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Ent_CapabilityProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UEnt_CapabilityProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Transient capability-advertisement seam.
 *
 * A capability is a tag-keyed "this object can do X" advertisement (Ent.Cap.*). Any UObject — a trait,
 * a component, an actor, a subsystem — can implement this interface to expose what it offers. Consumers
 * (AI brains, interaction systems, UI) ask an entity which capabilities it provides and then fetch the
 * backing object to invoke domain-specific APIs (usually itself via another seam interface).
 *
 * IMPORTANT — this is a QUERY-PER-USE seam. The mapping from capability tag to backing object is
 * VOLATILE: traits are added/removed at runtime, components stream in and out, network relevancy
 * changes which objects exist. Callers MUST query immediately before use and MUST NEVER cache the
 * result of GetCapabilityObject (cache the tag, not the pointer). A cached pointer can dangle after a
 * trait is removed or an object is GC'd, and on clients the backing object may not even be replicated.
 */
class DESIGNPATTERNSENTITY_API IEnt_CapabilityProvider
{
	GENERATED_BODY()

public:
	/**
	 * Append every capability tag this provider currently offers into OutCapabilities.
	 * Implementations append (never reset) so a caller can aggregate across several providers.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|Capability")
	void GetProvidedCapabilities(FGameplayTagContainer& OutCapabilities) const;

	/**
	 * True if this provider currently offers CapabilityTag. Exact-tag semantics by default; a provider
	 * may broaden to hierarchy matching, but consumers should not assume it.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|Capability")
	bool HasCapability(FGameplayTag CapabilityTag) const;

	/**
	 * Resolve the object that backs CapabilityTag (the thing a consumer casts to the relevant domain
	 * seam and calls). Returns nullptr when not provided. NEVER cache the returned pointer — re-query
	 * each time you need it, because traits/components backing a capability come and go at runtime.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity|Capability")
	UObject* GetCapabilityObject(FGameplayTag CapabilityTag) const;
};
