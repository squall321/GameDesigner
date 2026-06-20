// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Ent_Entity.generated.h"

// Forward-declare the entity-core component implemented by the spine area. This header is the contract
// the spine area fulfils; it must not hard-include the component (which lives downstream of this seam).
class UEnt_EntityComponent;

UINTERFACE(BlueprintType, MinimalAPI)
class UEnt_Entity : public UInterface
{
	GENERATED_BODY()
};

/**
 * Marker seam for "this actor is a DesignPatterns entity".
 *
 * An actor that carries a UEnt_EntityComponent implements this so other systems can discover the
 * component without hard-coding the component class or walking the component list. Typical use:
 *
 *     if (const IEnt_Entity* Ent = Cast<IEnt_Entity>(SomeActor))
 *         if (UEnt_EntityComponent* C = IEnt_Entity::Execute_GetEntityComponent(SomeActor))
 *             ... query traits / capabilities / identity ...
 *
 * Keeping this as a one-method interface lets the spine area choose how the component is stored and
 * lets non-actor owners (or proxies) participate by returning the relevant component.
 */
class DESIGNPATTERNSENTITY_API IEnt_Entity
{
	GENERATED_BODY()

public:
	/**
	 * Return the entity component that owns this actor's trait/capability/identity state, or nullptr
	 * if not yet constructed. Implementations should return the same component for the lifetime of the
	 * owner. The returned pointer is owned by the actor — do not store it across frames; re-query.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Entity")
	UEnt_EntityComponent* GetEntityComponent() const;
};
