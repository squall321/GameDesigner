// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Net/Seam_NetValue.h"
#include "Rep_HighlightTypes.generated.h"

/**
 * One auto-detected "highlight moment" on a recorded replay.
 *
 * Deliberately FLAT and SaveGame-friendly (no UObject refs, no FInstancedStruct): a focus entity id,
 * a kind tag (Rep.Highlight.*), the anchor time and the clip in/out window, a score for ranking, and
 * a closed-variant payload echoing the triggering magnitude. Because it is UObject-free it is also
 * safe to copy across a deferred lambda for off-thread file export.
 *
 * The in/out window is what an exported clip / reel uses; the anchor is the precise moment (used for
 * seek-to / killcam framing). Times are demo-relative seconds, matching FRep_ReplayEvent::Time.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_HighlightMoment
{
	GENERATED_BODY()

	/** Stable id for this moment within a session (so UI/clip controllers can address it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	FSeam_EntityId MomentId;

	/** Highlight kind (child of Rep.Highlight.*): MultiKill / Clutch / Objective ... */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	FGameplayTag KindTag;

	/** The subject the highlight is about (the player who scored), if known. Invalid => unattributed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	FSeam_EntityId FocusEntity;

	/** The precise demo-relative time (seconds) of the peak moment — used for seek/killcam framing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	float AnchorTimeSeconds = 0.f;

	/** Clip in-point (demo seconds); export/reel begins here. <= AnchorTimeSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	float InTimeSeconds = 0.f;

	/** Clip out-point (demo seconds); export/reel ends here. >= AnchorTimeSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	float OutTimeSeconds = 0.f;

	/** Ranking score (higher = more interesting); used to sort the reel and to drop low moments at cap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	float Score = 0.f;

	/** Number of contributing events that fell inside the detection window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	int32 ContributingEventCount = 0;

	/** Closed-variant payload echoing the triggering magnitude (e.g. summed score delta). Net/save-safe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	FSeam_NetValue Magnitude;

	/** Optional human label shown in the reel UI; empty falls back to the kind tag leaf name. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	FText DisplayLabel;

	/** The clip duration (out - in), clamped non-negative. */
	float GetClipDuration() const { return FMath::Max(0.f, OutTimeSeconds - InTimeSeconds); }

	/** True when this moment carries a usable kind tag and a non-empty clip window. */
	bool IsValid() const { return KindTag.IsValid() && OutTimeSeconds > InTimeSeconds; }

	/** Higher score sorts first; ties broken by earlier anchor. */
	bool operator<(const FRep_HighlightMoment& Other) const
	{
		if (!FMath::IsNearlyEqual(Score, Other.Score))
		{
			return Score > Other.Score;
		}
		return AnchorTimeSeconds < Other.AnchorTimeSeconds;
	}
};

/**
 * A named, ordered collection of highlight moments (the "reel"): the export unit for a highlights
 * package. Flat + SaveGame-friendly so it round-trips through a sidecar file.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_HighlightReel
{
	GENERATED_BODY()

	/** The replay name this reel was built from (the demo stream name). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	FString ReplayName;

	/** Friendly title for the reel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	FText Title;

	/** The moments, in reel order (the producer sorts by score or by time as it sees fit). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Highlight")
	TArray<FRep_HighlightMoment> Moments;

	/** Total clip seconds across all moments (informational). */
	float GetTotalClipSeconds() const
	{
		float Total = 0.f;
		for (const FRep_HighlightMoment& M : Moments)
		{
			Total += M.GetClipDuration();
		}
		return Total;
	}
};
