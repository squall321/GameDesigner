// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "HUD_Trackable.generated.h"

UINTERFACE(BlueprintType, MinimalAPI, meta = (DisplayName = "HUD Trackable"))
class UHUD_Trackable : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam implemented by anything that wants to appear as a marker on the minimap / world-marker layer.
 *
 * The minimap projection + ViewModel are decoupled from concrete actor/component types entirely through
 * this interface: the marker registry stores weak interface refs, and the ViewModel reads a world location,
 * an icon-selecting marker tag, and a per-frame visibility flag. A trackable can be an actor, an actor
 * component (the shipped UHUD_MarkerComponent), or any game-authored UObject that can answer these three
 * questions — gameplay never references the minimap, and the minimap never references gameplay.
 *
 * All three accessors are read-only projections of already-known state; implementations must be cheap and
 * side-effect free (they are polled once per projection refresh for every registered trackable).
 */
class DESIGNPATTERNSHUD_API IHUD_Trackable
{
	GENERATED_BODY()

public:
	/**
	 * World-space location used to project this marker onto minimap/screen space.
	 * For a component this is typically the owning actor's location; for a free UObject it is whatever
	 * point the game wants tracked.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|HUD|Trackable")
	FVector GetWorldLocation() const;

	/**
	 * Icon-selecting marker kind tag (e.g. DP.HUD.Marker.Enemy). The ViewModel maps this tag to an icon
	 * via its tag->icon table; an empty/unmapped tag falls back to the default icon.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|HUD|Trackable")
	FGameplayTag GetMarkerTag() const;

	/**
	 * Per-frame visibility gate. Return false to be skipped this refresh (e.g. undiscovered, stealthed,
	 * out of the player's faction reveal) without unregistering — registration is for lifetime, this is
	 * for moment-to-moment culling.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|HUD|Trackable")
	bool IsVisibleOnMap() const;
};
