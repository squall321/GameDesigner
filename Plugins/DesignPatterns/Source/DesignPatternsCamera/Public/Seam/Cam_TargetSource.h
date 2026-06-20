// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Cam_TargetSource.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class UCam_TargetSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read-only "who am I targeting" seam.
 *
 * The Camera module's lock-on / targeting component implements this so that OTHER systems
 * (a HUD reticle, an aim-assist consumer, an ability that needs the current focus) can read
 * the player's current target WITHOUT depending on the Camera module's concrete component type
 * and WITHOUT ever touching a raw AActor*.
 *
 * The target is exposed strictly by its stable FSeam_EntityId (net/save-stable, hashable),
 * never by pointer, so:
 *   - consumers cannot accidentally keep a despawned actor alive,
 *   - the id is meaningful across the network (the same enemy has the same id on every machine),
 *   - the value can be persisted / round-tripped through save or replication if a consumer chooses.
 *
 * Resolving an actual actor from the id (when a consumer genuinely needs the AActor*) is the
 * consumer's job via the identity registry / ISeam_EntityIdentity lookups — this seam deliberately
 * does not hand out pointers.
 *
 * This is a PURE READ seam: it has no setters. Targeting is driven by the implementing component's
 * own input/logic; observers only ever read.
 */
class DESIGNPATTERNSCAMERA_API ICam_TargetSource
{
	GENERATED_BODY()

public:
	/**
	 * The stable id of the entity currently targeted / locked-on.
	 * @return the current target id, or FSeam_EntityId::Invalid() when nothing is targeted.
	 *         Always check HasTarget() (or FSeam_EntityId::IsValid() on the result) before use.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Camera|Target")
	FSeam_EntityId GetCurrentTarget() const;

	/** @return true when a valid target is currently selected / locked. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Camera|Target")
	bool HasTarget() const;
};
