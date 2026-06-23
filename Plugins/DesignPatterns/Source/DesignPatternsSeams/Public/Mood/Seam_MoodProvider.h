// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_MoodProvider.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_MoodProvider : public UInterface
{
	GENERATED_BODY()
};

/**
 * Shared "mood / emotion" read seam. A per-agent emotion model exposes its axes here so the agent brain
 * (and any UI) can read mood WITHOUT depending on the concrete SimAgents component — exactly mirroring
 * how ISeam_NeedProvider exposes needs.
 *
 * HOUSE STYLE — BlueprintNativeEvent UINTERFACE (sibling to ISeam_NeedProvider, NOT raw-virtual like
 * ISeam_EntityRelationshipRead). The implementer is a per-actor UActorComponent (USimAg_MoodComponent),
 * so consumers resolve it as `TScriptInterface<ISeam_MoodProvider>` off the owning actor and invoke it
 * through the generated `Execute_` thunks — the same mechanism as ISimAg_Locomotion. This is a
 * project-/component-supplied bridge, hence native-event rather than a raw internal read seam.
 *
 * MOOD AXES ARE TAGS — every axis is an FGameplayTag under a project's `Mood.*` hierarchy (e.g.
 * Mood.Happiness, Mood.Stress, Mood.Anger). There is deliberately no ESeam_MoodAxis enum, matching the
 * framework's tag-everywhere rule.
 *
 * THREAD / AUTHORITY — both methods are const reads, safe on server and clients (the emotion model is
 * replicated, so every machine can answer from its local copy). They never mutate game state.
 */
class DESIGNPATTERNSSEAMS_API ISeam_MoodProvider
{
	GENERATED_BODY()

public:
	/**
	 * Normalized [0,1] intensity of the mood axis MoodTag, where 0.5 is the neutral baseline by
	 * convention (so a brain can treat <0.5 as "below baseline"). Returns 0.5 (neutral) when the axis
	 * is unknown, so a missing axis never skews a consumer's math.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mood")
	float GetMoodNormalized(FGameplayTag MoodTag) const;

	/**
	 * Multiplier a need-scoring consumer should apply to the URGENCY of NeedTag given the agent's
	 * current mood (e.g. a stressed agent over-weights its Social need, a content agent under-weights
	 * Fun). 1.0 means "no mood influence"; the implementer maps relevant axes onto this scalar. Returns
	 * 1.0 (no influence) when the provider has nothing to say about NeedTag — a documented inert default.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Mood")
	float GetNeedWeightMultiplier(FGameplayTag NeedTag) const;
};
