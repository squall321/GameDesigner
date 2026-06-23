// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Highlight/Rep_ClipController.h"
#include "Playback/Rep_PlaybackController.h"
#include "Core/DPLog.h"

void URep_ClipController::BindPlayback(URep_PlaybackController* InController)
{
	Playback = InController;
}

void URep_ClipController::PlayClip(const FRep_HighlightMoment& Moment)
{
	URep_PlaybackController* Controller = Playback.Get();
	if (!Controller || !Controller->IsPlaybackActive())
	{
		UE_LOG(LogDP, Warning, TEXT("ClipController: PlayClip with no active playback controller."));
		return;
	}

	if (!Moment.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("ClipController: PlayClip given an invalid moment."));
		return;
	}

	CurrentMoment = Moment;

	// Seek to the in-point and play forward at the configured highlight speed (defaults from settings
	// via the controller's own clamp). Use the developer-settings clip framing already baked into the
	// moment's In/Out times — here we only need to start at In.
	Controller->SeekToTime(CurrentMoment.InTimeSeconds);
	Controller->SetPaused(false);

	bPlaying = true;
	ElapsedWatchSeconds = 0.f;

	OnClipStarted.Broadcast(CurrentMoment);
	UE_LOG(LogDP, Verbose, TEXT("ClipController: playing clip [%.2f..%.2f] kind=%s"),
		CurrentMoment.InTimeSeconds, CurrentMoment.OutTimeSeconds, *CurrentMoment.KindTag.ToString());
}

void URep_ClipController::StopClip()
{
	bPlaying = false;
	ElapsedWatchSeconds = 0.f;
}

void URep_ClipController::Tick(float DeltaSeconds)
{
	if (!bPlaying)
	{
		return;
	}

	URep_PlaybackController* Controller = Playback.Get();
	if (!Controller || !Controller->IsPlaybackActive())
	{
		// Playback world torn down mid-clip; end gracefully.
		StopClip();
		OnClipFinished.Broadcast();
		return;
	}

	ElapsedWatchSeconds += DeltaSeconds;

	// Detect the out-point from the REAL playback position (the seek/play is async, so polling the
	// driver is correct where a wall-clock timer would drift). A defensive max-watch guard (the clip
	// duration plus the seek lead the settings allow, scaled up) prevents a stuck async seek from
	// pinning us forever.
	const float CurrentTime = Controller->GetCurrentTime();
	const float MaxWatch = CurrentMoment.GetClipDuration() + 30.f; // generous async-seek allowance
	if (CurrentTime >= CurrentMoment.OutTimeSeconds || ElapsedWatchSeconds >= MaxWatch)
	{
		Controller->SetPaused(true);
		StopClip();
		OnClipFinished.Broadcast();
	}
}
