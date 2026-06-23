// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Bookmark/Rep_BookmarkController.h"
#include "Timeline/Rep_ReplayTimeline.h"
#include "Playback/Rep_PlaybackController.h"
#include "DesignPatternsReplayModule.h"
#include "Core/DPLog.h"

void URep_BookmarkController::Bind(URep_ReplayTimeline* InTimeline, URep_PlaybackController* InPlayback)
{
	Timeline = InTimeline;
	Playback = InPlayback;
}

// ---------------------------------------------------------------------------------------------
// Authoring
// ---------------------------------------------------------------------------------------------

void URep_BookmarkController::AddBookmark(const FText& Label)
{
	if (URep_ReplayTimeline* TL = Timeline.Get())
	{
		// Forwards to the existing convenience that stamps Rep.Event.Bookmark at the current time.
		TL->AddBookmark(Label);
		OnBookmarksChanged.Broadcast();
	}
}

void URep_BookmarkController::AddAnnotationAt(float TimeSeconds, const FText& Annotation)
{
	if (URep_ReplayTimeline* TL = Timeline.Get())
	{
		FRep_ReplayEvent Event(FMath::Max(0.f, TimeSeconds), Rep_NativeTags::Event_Bookmark, Annotation);
		// bUseProvidedTime so the annotation lands at the explicit playback time, not "now".
		TL->RecordEvent(Event, /*bUseProvidedTime*/ true);
		OnBookmarksChanged.Broadcast();
	}
}

void URep_BookmarkController::AddChapterAt(float TimeSeconds, const FText& ChapterTitle)
{
	if (URep_ReplayTimeline* TL = Timeline.Get())
	{
		FRep_ReplayEvent Event(FMath::Max(0.f, TimeSeconds), Rep_NativeTags::Event_Chapter, ChapterTitle);
		TL->RecordEvent(Event, /*bUseProvidedTime*/ true);
		OnBookmarksChanged.Broadcast();
	}
}

// ---------------------------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------------------------

bool URep_BookmarkController::SeekToEvent(const FRep_ReplayEvent& Event)
{
	if (URep_PlaybackController* PC = Playback.Get())
	{
		PC->SeekToEvent(Event);
		return true;
	}
	return false;
}

bool URep_BookmarkController::SeekToNextBookmark(float FromTime)
{
	URep_ReplayTimeline* TL = Timeline.Get();
	if (!TL)
	{
		return false;
	}

	// Consider both bookmarks and chapters: find the earliest of the two "next" candidates.
	FRep_ReplayEvent NextBookmark;
	const bool bHasBookmark = TL->FindNextEvent(FromTime, Rep_NativeTags::Event_Bookmark, NextBookmark);

	FRep_ReplayEvent NextChapter;
	const bool bHasChapter = TL->FindNextEvent(FromTime, Rep_NativeTags::Event_Chapter, NextChapter);

	if (!bHasBookmark && !bHasChapter)
	{
		return false;
	}

	const FRep_ReplayEvent& Target =
		(bHasBookmark && (!bHasChapter || NextBookmark.Time <= NextChapter.Time)) ? NextBookmark : NextChapter;
	return SeekToEvent(Target);
}

bool URep_BookmarkController::SeekToPreviousBookmark(float FromTime)
{
	URep_ReplayTimeline* TL = Timeline.Get();
	if (!TL)
	{
		return false;
	}

	FRep_ReplayEvent PrevBookmark;
	const bool bHasBookmark = TL->FindPreviousEvent(FromTime, Rep_NativeTags::Event_Bookmark, PrevBookmark);

	FRep_ReplayEvent PrevChapter;
	const bool bHasChapter = TL->FindPreviousEvent(FromTime, Rep_NativeTags::Event_Chapter, PrevChapter);

	if (!bHasBookmark && !bHasChapter)
	{
		return false;
	}

	// The latest of the two "previous" candidates.
	const FRep_ReplayEvent& Target =
		(bHasBookmark && (!bHasChapter || PrevBookmark.Time >= PrevChapter.Time)) ? PrevBookmark : PrevChapter;
	return SeekToEvent(Target);
}

bool URep_BookmarkController::SeekToNextChapter(float FromTime)
{
	URep_ReplayTimeline* TL = Timeline.Get();
	if (!TL)
	{
		return false;
	}
	FRep_ReplayEvent NextChapter;
	if (TL->FindNextEvent(FromTime, Rep_NativeTags::Event_Chapter, NextChapter))
	{
		return SeekToEvent(NextChapter);
	}
	return false;
}

// ---------------------------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------------------------

TArray<FRep_BookmarkEntry> URep_BookmarkController::GetBookmarks() const
{
	TArray<FRep_BookmarkEntry> Out;
	const URep_ReplayTimeline* TL = Timeline.Get();
	if (!TL)
	{
		return Out;
	}

	for (const FRep_ReplayEvent& Event : TL->GetEvents())
	{
		const bool bIsChapter  = Event.EventTag.MatchesTag(Rep_NativeTags::Event_Chapter);
		const bool bIsBookmark = Event.EventTag.MatchesTag(Rep_NativeTags::Event_Bookmark);
		if (bIsChapter || bIsBookmark)
		{
			FRep_BookmarkEntry Entry;
			Entry.Event = Event;
			Entry.bIsChapter = bIsChapter;
			Out.Add(Entry);
		}
	}
	return Out;
}

TArray<FRep_BookmarkEntry> URep_BookmarkController::GetChapters() const
{
	TArray<FRep_BookmarkEntry> Out;
	const URep_ReplayTimeline* TL = Timeline.Get();
	if (!TL)
	{
		return Out;
	}

	for (const FRep_ReplayEvent& Event : TL->GetEvents())
	{
		if (Event.EventTag.MatchesTag(Rep_NativeTags::Event_Chapter))
		{
			FRep_BookmarkEntry Entry;
			Entry.Event = Event;
			Entry.bIsChapter = true;
			Out.Add(Entry);
		}
	}
	return Out;
}
