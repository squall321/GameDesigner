// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mood/Seam_MoodProvider.h"

// INERT native defaults for the mood seam. An object that implements ISeam_MoodProvider but does not
// override these (or a project with no emotion model at all) reports a neutral mood and no need-weight
// influence, so every consumer's math is unaffected. The SimAgents emotion component overrides both to
// read its replicated mood axes.

float ISeam_MoodProvider::GetMoodNormalized_Implementation(FGameplayTag /*MoodTag*/) const
{
	// Neutral baseline by convention: a consumer treating <0.5 as "below baseline" sees no skew.
	return 0.5f;
}

float ISeam_MoodProvider::GetNeedWeightMultiplier_Implementation(FGameplayTag /*NeedTag*/) const
{
	// 1.0 == no mood influence on need urgency.
	return 1.0f;
}
