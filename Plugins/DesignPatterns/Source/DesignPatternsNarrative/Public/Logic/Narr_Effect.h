// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
// The UNarr_Effect base + the world-state write leaves (UNarr_Effect_SetFlag / _AddCounter) are owned by
// the story-director area's effect header; the source interface is owned by the Seam header. We include
// them here and only ADD a net-new leaf, so each shared type has exactly one definition module-wide.
#include "Dialogue/Narr_StoryConditionTypes.h"   // UNarr_Effect (base) + write leaves
#include "Seam/Narr_StoryConditionSource.h"       // INarr_StoryConditionSource
#include "Narr_Effect.generated.h"

/**
 * Net-new dialogue effect leaf that EXTENDS the shared effect mini-language.
 *
 * The base UNarr_Effect and the authoritative write leaves are owned elsewhere (see the include above);
 * this header adds only the effect the dialogue area needs that the shared write set lacks: an
 * OBSERVER-ONLY message-bus broadcast.
 */

/**
 * Effect: broadcast an OBSERVER-ONLY message-bus event (a designer-authored story cue).
 *
 * COSMETIC / LOCAL: this never mutates authoritative state, so unlike the hub-writing effects it does not
 * route through the source's write API — it broadcasts directly on the local message bus (resolved via
 * the source's world context) on whatever machine runs it. Listeners (UI fx / audio / analytics) react;
 * the event NEVER drives dialogue or story flow.
 */
UCLASS(meta = (DisplayName = "Broadcast Bus Event"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Effect_BroadcastBusEvent : public UNarr_Effect
{
	GENERATED_BODY()

public:
	/**
	 * The bus channel to broadcast on. Anchor under DP.Bus.Narrative; defaults to
	 * DP.Bus.Narrative.StoryEvent when left unset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect", meta = (Categories = "DP.Bus.Narrative"))
	FGameplayTag Channel;

	/** A free-form event id carried in the flat payload (SecondaryTag) for listeners to switch on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect")
	FGameplayTag EventId;

	/** Optional integer payload (an arbitrary designer value carried to listeners). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect")
	int32 IntValue = 0;

	//~ Begin UNarr_Effect
	virtual void Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeEffect() const override;
	//~ End UNarr_Effect
};
