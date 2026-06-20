// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Lvl_InterestSource.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class ULvl_InterestSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * "Interest source" seam — anything the streaming director should keep content loaded around.
 *
 * Typically implemented by a local player pawn, a spectator camera, a follow camera, or a
 * cinematic rig. The director sums every registered source's location+radius into the set of
 * streaming queries it issues each frame, so multiple split-screen players or a wide camera can
 * all keep their surroundings resident.
 *
 * The director only ever READS these; it never mutates a source. Sources are held WEAKLY by the
 * director and null-checked on use, so a destroyed pawn simply stops contributing — there is no
 * lifetime contract beyond "implement the two getters while you want to be a streaming center".
 *
 * Implemented as a UObject interface (not a seam in the shared Seams module) because interest is a
 * concept owned by this module; consumers outside the module register through the director's public
 * Register/UnregisterInterestSource API rather than implementing the interface themselves.
 */
class DESIGNPATTERNSLEVELDIRECTOR_API ILvl_InterestSource
{
	GENERATED_BODY()

public:
	/**
	 * World-space location this source wants content streamed around. For a pawn this is usually the
	 * actor location; for a camera it may be the camera's view origin (which can lead the pawn).
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|LevelDirector|Interest")
	FVector GetInterestLocation() const;

	/**
	 * Additional radius (world units) the director should keep resident around GetInterestLocation,
	 * ON TOP OF the policy's distance bands. Lets a fast vehicle or a sniper widen its own bubble.
	 * Return 0 to rely purely on the policy bands. Negative values are treated as 0 by the director.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|LevelDirector|Interest")
	float GetInterestRadius() const;
};
