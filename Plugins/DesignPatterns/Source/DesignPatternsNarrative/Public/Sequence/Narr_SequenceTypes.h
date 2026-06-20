// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MovieSceneSequencePlaybackSettings.h"
#include "Narr_SequenceTypes.generated.h"

/** Why a cutscene playback ended — distinguishes a natural finish from a player skip / abort. */
UENUM(BlueprintType)
enum class ENarr_SequenceEndReason : uint8
{
	/** The sequence played to its end and stopped naturally. */
	Finished,

	/** The player skipped the cutscene before it finished. */
	Skipped,

	/** Playback was aborted programmatically (e.g. the owning actor was torn down). */
	Aborted
};

/**
 * Flat, weak-ref-free bus payload describing a cutscene/sequence lifecycle event.
 *
 * Carried inside an FInstancedStruct on the message bus. Holds only tags + an end-reason enum (no
 * UObject/weak refs) so it is safe to queue for deferred dispatch.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_SequenceEventPayload
{
	GENERATED_BODY()

	/** Designer-authored identity of the sequence (the trigger/director's SequenceTag). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|Sequence")
	FGameplayTag SequenceTag;

	/** The end reason for finish/skip events (ignored for the start event). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|Sequence")
	ENarr_SequenceEndReason EndReason = ENarr_SequenceEndReason::Finished;

	FNarr_SequenceEventPayload() = default;
	explicit FNarr_SequenceEventPayload(const FGameplayTag& InTag,
		ENarr_SequenceEndReason InReason = ENarr_SequenceEndReason::Finished)
		: SequenceTag(InTag), EndReason(InReason) {}
};

/**
 * Designer-tunable playback configuration for a cutscene.
 *
 * Wraps the engine's FMovieSceneSequencePlaybackSettings with the extra narrative-policy knobs the
 * director needs (input lock, skippability, completion flag). All values are EditAnywhere so they are
 * authored per trigger / per Play call rather than hardcoded.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_SequencePlayParams
{
	GENERATED_BODY()

	/** Engine-native playback settings (loop count, play rate, start offset, restore-state, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence")
	FMovieSceneSequencePlaybackSettings PlaybackSettings;

	/**
	 * When true the director pushes DP.InputMode.Cutscene onto the shared input-mode arbiter for the
	 * duration of playback, and pops it on end. Default true (cutscenes typically lock input).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence")
	bool bLockInput = true;

	/**
	 * Priority used for the input-mode push. Higher beats lower on the shared arbiter. Defensive
	 * default chosen above a typical HUD-menu priority so a cutscene lock wins over menus; projects
	 * override per cutscene.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence",
		meta = (EditCondition = "bLockInput"))
	int32 InputLockPriority = 200;

	/** When true the player may skip this cutscene (Skip() stops it early). Default true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence")
	bool bSkippable = true;

	/**
	 * Optional world-hub flag set true (AUTHORITY side) when this cutscene COMPLETES, so persistent /
	 * replicated story state records that the cutscene was seen. Cosmetic playback is local, but its
	 * completion is an authoritative fact. Leave empty for cutscenes with no story consequence.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence",
		meta = (Categories = "DP.WorldHub"))
	FGameplayTag CompletionHubFlag;

	/**
	 * When true a skip still sets CompletionHubFlag (the cutscene counts as "seen" even when skipped);
	 * when false only a natural finish sets it. Default true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence")
	bool bCompletionFlagOnSkip = true;

	FNarr_SequencePlayParams() = default;
};
