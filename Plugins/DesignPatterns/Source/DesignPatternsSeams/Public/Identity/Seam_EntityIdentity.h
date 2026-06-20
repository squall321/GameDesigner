// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Seam_EntityIdentity.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_EntityIdentity : public UInterface
{
	GENERATED_BODY()
};

/**
 * Stable-identity seam. The Entity module's component implements this; World/Grid/Agents key their
 * per-entity state off GetEntityId() without depending on the Entity module's concrete type. The
 * archetype tag lets systems branch on "what kind of thing is this" generically.
 */
class DESIGNPATTERNSSEAMS_API ISeam_EntityIdentity
{
	GENERATED_BODY()

public:
	/** The net/save-stable id of this entity. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Identity")
	FSeam_EntityId GetEntityId() const;

	/** The archetype tag (what kind of entity this is), for generic type branching. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Identity")
	FGameplayTag GetArchetypeTag() const;
};
