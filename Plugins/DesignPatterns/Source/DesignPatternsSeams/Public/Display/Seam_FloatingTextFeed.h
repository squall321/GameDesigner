// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Net/Seam_NetValue.h"
#include "Seam_FloatingTextFeed.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_FloatingTextFeed : public UInterface
{
	GENERATED_BODY()
};

/**
 * PROJECT-BRIDGE / UI seam onto a "floating combat text" (damage numbers, heal popups, pickup toasts)
 * presentation feed.
 *
 * This is the shared owner of the damage-number / floating-text overlap. PRODUCERS push transient,
 * world-anchored text events (Combat on a hit, the Replay killcam re-surfacing the lethal hit during
 * a death cam) and a single CONSUMER — the HUD's floating-text overlay widget — implements this seam
 * and renders them. Producers NEVER hard-include the HUD; they resolve a
 * TScriptInterface<ISeam_FloatingTextFeed> from the service locator under a Display service key
 * (DP.Service.Display.FloatingText) and hold it WEAKLY, re-resolving on use.
 *
 * WIRE-SAFETY: the magnitude is carried as an FSeam_NetValue (the closed bool/int/float/vector/tag/name
 * union), NEVER a raw FInstancedStruct — so a producer that fires this from replicated combat state
 * cannot smuggle an unserializable payload across. The anchor is an FSeam_EntityId (net/save-stable)
 * rather than a raw actor pointer, so the feed can resolve the on-screen anchor itself (or fall back to
 * a screen-space toast when the entity is off-screen / unknown).
 *
 * BlueprintNativeEvent UINTERFACE (UI-class seam house style) so the HUD may implement it in Blueprint.
 * The shipped default implementation here is INERT (PushFloatingText is a no-op), so a project with no
 * floating-text overlay still links and producers simply no-op — keeping the seam genre-agnostic.
 *
 * THREADING: game-thread only. Purely COSMETIC and LOCAL — pushing floating text never mutates any
 * replicated/authoritative state; each machine renders its own floating text from its own local events.
 */
class DESIGNPATTERNSSEAMS_API ISeam_FloatingTextFeed
{
	GENERATED_BODY()

public:
	/**
	 * Push one transient floating-text event for the overlay to display.
	 *
	 * @param Anchor    Net/save-stable id of the world entity the text should float over; an invalid
	 *                  id is a documented request for a screen-anchored (non-world) toast.
	 * @param Text      The already-localized display text (e.g. a formatted number or label).
	 * @param StyleTag  Style/category selector (child of a project DP.Display.FloatingText.* tree:
	 *                  Damage / Heal / Crit / Pickup ...) the overlay maps to colour/size/animation.
	 * @param Magnitude Closed-variant numeric magnitude (e.g. the damage amount) for sizing/sorting;
	 *                  unset when the text is purely qualitative.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Display")
	void PushFloatingText(FSeam_EntityId Anchor, FText Text, FGameplayTag StyleTag, FSeam_NetValue Magnitude);
};
