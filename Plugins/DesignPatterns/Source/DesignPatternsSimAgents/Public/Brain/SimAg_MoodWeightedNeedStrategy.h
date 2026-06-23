// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Brain/SimAg_NeedStrategy.h"
#include "SimAg_MoodWeightedNeedStrategy.generated.h"

/**
 * ADDITIVE subclass of the shipped USimAg_NeedStrategy that folds the agent's MOOD into need scoring.
 * It reuses the base's need resolution + urgency curve + Execute verbatim, then multiplies the base score
 * by the owner's ISeam_MoodProvider::GetNeedWeightMultiplier(NeedTag) — so a stressed agent over-weights
 * its Social need and a content one under-weights Fun, with no edit to the base strategy.
 *
 * Drop-in: authored inline in any brain where mood-aware need scoring is wanted. When no mood provider is
 * present the multiplier is the seam's inert 1.0 default, so behaviour is identical to the base strategy.
 */
UCLASS(EditInlineNew, DefaultToInstanced, meta = (DisplayName = "SimAg Mood-Weighted Need Strategy"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_MoodWeightedNeedStrategy : public USimAg_NeedStrategy
{
	GENERATED_BODY()

public:
	USimAg_MoodWeightedNeedStrategy();

	//~ Begin UDP_Strategy
	virtual float ScoreFor_Implementation(const FDP_StrategyContext& Context) const override;
	//~ End UDP_Strategy
};
