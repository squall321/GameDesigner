// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// The UNarr_Effect base is owned by the story-director area's effect header; include it (do not redefine).
#include "Dialogue/Narr_StoryConditionTypes.h"   // UNarr_Effect (base) + INarr_StoryConditionSource
#include "Narr_Effect_AdjustReputation.generated.h"

/**
 * Net-new effect leaf that adjusts faction/NPC standing on a dialogue choice or quest outcome.
 *
 * Extends the shared UNarr_Effect mini-language. Routes the adjustment through the shared ISeam_Reputation
 * owner (resolved under DP.Service.Narrative.Reputation via the Source's world context). The owner's
 * AddFactionReputation / AddNpcAffinity guard authority at the TOP, so applying this effect on a client is a
 * safe no-op (the new standing replicates back through the world hub). Mirrors how the shipped write effects
 * route through the authority-guarded source.
 */
UCLASS(meta = (DisplayName = "Adjust Reputation"))
class DESIGNPATTERNSNARRATIVE_API UNarr_Effect_AdjustReputation : public UNarr_Effect
{
	GENERATED_BODY()

public:
	/** The faction or NPC tag whose standing to adjust. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect")
	FGameplayTag FactionOrNpcTag;

	/** The signed amount to add to the standing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect")
	float Delta = 0.f;

	/** When true the tag addresses a per-NPC affinity (AddNpcAffinity); when false a faction standing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Effect")
	bool bIsNpcAffinity = false;

	//~ Begin UNarr_Effect
	virtual void Apply_Implementation(const TScriptInterface<INarr_StoryConditionSource>& Source) const override;
	virtual FString DescribeEffect() const override;
	//~ End UNarr_Effect
};
