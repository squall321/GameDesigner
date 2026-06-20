// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Story/Narr_StoryBeatDataAsset.h"
#include "Logic/Narr_Condition.h"                 // UNarr_Condition::IsMet
#include "Dialogue/Narr_StoryConditionTypes.h"    // UNarr_Effect::Apply
#include "Seam/Narr_StoryConditionSource.h"
#include "Core/DPLog.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UNarr_StoryBeatDataAsset::UNarr_StoryBeatDataAsset()
{
	// No defaults beyond the UPROPERTY initializers; identity (DataTag) is authored per asset.
}

bool UNarr_StoryBeatDataAsset::ArePrerequisitesMet(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	for (const TObjectPtr<UNarr_Condition>& Cond : Prerequisites)
	{
		// A null entry is an absent gate (passes). A real condition is evaluated against the source via
		// the shared condition mini-language (same path the dialogue runner uses).
		if (Cond && !Cond->IsMet(Source))
		{
			return false;
		}
	}
	return true;
}

void UNarr_StoryBeatDataAsset::ApplyCompletionEffects(const TScriptInterface<INarr_StoryConditionSource>& Source) const
{
	for (const TObjectPtr<UNarr_Effect>& Effect : CompletionEffects)
	{
		if (Effect)
		{
			// Each effect routes writes through the source's authority-guarded API (no-op on clients).
			Effect->Apply(Source);
		}
	}
}

FName UNarr_StoryBeatDataAsset::GetDataAssetType_Implementation() const
{
	// All beat subclasses share one bucket so the asset manager indexes the whole story graph together.
	static const FName StoryBeatType(TEXT("Narr_StoryBeat"));
	return StoryBeatType;
}

#if WITH_EDITOR
EDataValidationResult UNarr_StoryBeatDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (!ArcTag.IsValid())
	{
		Context.AddWarning(FText::FromString(
			FString::Printf(TEXT("Story beat '%s' has no ArcTag; arc-level events/save grouping will be skipped."),
				*DataTag.ToString())));
	}

	// A beat that neither auto-completes nor names any next beats is a terminal beat — that's fine, but
	// a beat that auto-completes with no next beats AND no completion effects is almost certainly a
	// mistake (it does nothing observable).
	if (bAutoCompleteOnActivate && NextBeats.Num() == 0 && CompletionEffects.Num() == 0)
	{
		Context.AddWarning(FText::FromString(
			FString::Printf(TEXT("Story beat '%s' auto-completes but has no NextBeats and no CompletionEffects (no-op)."),
				*DataTag.ToString())));
	}

	// Self-reference in NextBeats would loop forever.
	if (NextBeats.Contains(DataTag))
	{
		Context.AddError(FText::FromString(
			FString::Printf(TEXT("Story beat '%s' lists itself in NextBeats (would self-loop)."),
				*DataTag.ToString())));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif // WITH_EDITOR
