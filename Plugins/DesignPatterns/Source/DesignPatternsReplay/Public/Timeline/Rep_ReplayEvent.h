// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Rep_ReplayEvent.generated.h"

/**
 * A single significant moment on a replay timeline — the unit a scrubber turns into a marker
 * and can jump to.
 *
 * Kept deliberately flat and net/save-friendly: a relative time (seconds from the demo start),
 * an identity tag (Rep.Event.* or a game tag), a short display label, and ONE closed-variant
 * payload value (FSeam_NetValue — never a raw FInstancedStruct, so this struct can be stored in
 * the demo stream or a sidecar without bespoke serialization). Anything richer than the supported
 * variant set stays on the recording machine; the timeline only needs enough to label and seek.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_ReplayEvent
{
	GENERATED_BODY()

	/** Seconds from the start of the demo at which this event occurred. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay")
	float Time = 0.f;

	/** Stable identity of the event (anchor under Rep.Event.* or a game-authored tag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay")
	FGameplayTag EventTag;

	/**
	 * Optional human-readable label shown on the scrubber tooltip. Empty falls back to the
	 * event tag's leaf name at display time.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay")
	FText DisplayLabel;

	/** Closed-variant payload (e.g. a score delta, a team tag, a location). Net/save-safe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay")
	FSeam_NetValue Payload;

	FRep_ReplayEvent() = default;

	FRep_ReplayEvent(float InTime, const FGameplayTag& InTag, const FText& InLabel = FText::GetEmpty(),
		const FSeam_NetValue& InPayload = FSeam_NetValue())
		: Time(InTime)
		, EventTag(InTag)
		, DisplayLabel(InLabel)
		, Payload(InPayload)
	{
	}

	/** True when this event carries a usable identity tag. */
	bool IsValid() const { return EventTag.IsValid(); }

	/** Resolve the label to show: the explicit DisplayLabel, else the tag's leaf name. */
	FText GetEffectiveLabel() const
	{
		if (!DisplayLabel.IsEmpty())
		{
			return DisplayLabel;
		}
		if (EventTag.IsValid())
		{
			FString TagString = EventTag.ToString();
			int32 LastDot = INDEX_NONE;
			if (TagString.FindLastChar(TEXT('.'), LastDot))
			{
				TagString = TagString.RightChop(LastDot + 1);
			}
			return FText::FromString(TagString);
		}
		return FText::GetEmpty();
	}

	/** Chronological ordering helper for stable sorts. */
	bool operator<(const FRep_ReplayEvent& Other) const { return Time < Other.Time; }
};
