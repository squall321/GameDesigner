// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Timeline/Rep_ReplayEvent.h"
#include "Rep_BookmarkController.generated.h"

class URep_ReplayTimeline;
class URep_PlaybackController;

/**
 * A projected bookmark / chapter entry for the UI: the timeline event plus whether it is a coarse
 * chapter (Rep.Event.Chapter) or a fine bookmark (Rep.Event.Bookmark). Flat + copyable.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_BookmarkEntry
{
	GENERATED_BODY()

	/** The underlying timeline event (time / tag / label / payload). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Bookmark")
	FRep_ReplayEvent Event;

	/** True when this entry is a coarse chapter marker (vs a fine bookmark / annotation). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Bookmark")
	bool bIsChapter = false;
};

/** Fired when the bookmark/chapter set changes (an add, or an annotation edit). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRep_OnBookmarksChanged);

/**
 * URep_BookmarkController — viewer-authored bookmarks, annotations and chapter markers over the replay
 * timeline, plus navigation between them.
 *
 * It is a thin orchestrator over the EXISTING timeline + playback APIs: bookmarks are recorded with the
 * timeline's AddBookmark (Rep.Event.Bookmark), chapters with RecordEvent under Rep.Event.Chapter, and
 * navigation forwards to the timeline's FindNext/PreviousEvent + the playback controller's SeekToEvent.
 * Annotations are a label set on a fresh bookmark event. Nothing here decodes the demo or duplicates
 * the timeline's storage — chapters/bookmarks live in the same sidecar the timeline already flushes.
 *
 * Owned by the replay UI host; holds the timeline + controller weakly.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSREPLAY_API URep_BookmarkController : public UObject
{
	GENERATED_BODY()

public:
	/** Bind the timeline this controller reads/writes and the transport it seeks. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Bookmark")
	void Bind(URep_ReplayTimeline* InTimeline, URep_PlaybackController* InPlayback);

	// ---- Authoring ----

	/** Drop a fine bookmark at the current recording/playback-relative time (forwards to AddBookmark). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Bookmark")
	void AddBookmark(const FText& Label);

	/**
	 * Drop a fine bookmark at an explicit demo time with an annotation label. Used during playback to
	 * annotate a precise moment (the timeline records it under Rep.Event.Bookmark with the given time).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Bookmark")
	void AddAnnotationAt(float TimeSeconds, const FText& Annotation);

	/** Drop a coarse chapter marker at an explicit demo time (Rep.Event.Chapter). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Bookmark")
	void AddChapterAt(float TimeSeconds, const FText& ChapterTitle);

	// ---- Navigation ----

	/** Seek to the next bookmark/chapter at or after FromTime; returns false if none. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Bookmark")
	bool SeekToNextBookmark(float FromTime);

	/** Seek to the previous bookmark/chapter at or before FromTime; returns false if none. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Bookmark")
	bool SeekToPreviousBookmark(float FromTime);

	/** Seek to the next chapter at or after FromTime; returns false if none. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Bookmark")
	bool SeekToNextChapter(float FromTime);

	// ---- Query ----

	/** All bookmark + chapter entries in chronological order (rebuilt from the timeline on demand). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Bookmark")
	TArray<FRep_BookmarkEntry> GetBookmarks() const;

	/** Just the chapter markers, in chronological order. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Bookmark")
	TArray<FRep_BookmarkEntry> GetChapters() const;

	/** Broadcast when bookmarks/chapters change. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Bookmark")
	FRep_OnBookmarksChanged OnBookmarksChanged;

private:
	/** The timeline read/written (weak: owned by the replay subsystem). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_ReplayTimeline> Timeline;

	/** The transport seeked (weak: owned by the host). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_PlaybackController> Playback;

	/** Seek to an event via the bound controller; returns true if a controller was available. */
	bool SeekToEvent(const FRep_ReplayEvent& Event);
};
