// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/IClassDescriptor.h"
#include "GameplayTagContainer.h"
#include "Rep_ReplayViewModel.generated.h"

class URep_PlaybackController;
class URep_ReplayTimeline;
struct FRep_ReplayEvent;

/**
 * A scrubber marker projected for the UI: the normalized [0,1] position of a timeline event plus
 * its label and identity tag (so the view can colour/icon markers by kind). Flat and copyable.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_TimelineMarker
{
	GENERATED_BODY()

	/** Normalized position along the scrubber in [0,1]. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	float NormalizedPosition = 0.f;

	/** Absolute event time in seconds (for SeekToEvent). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	float TimeSeconds = 0.f;

	/** Display label for the marker tooltip. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	FText Label;

	/** Event identity tag (Rep.Event.* or game tag) so the view can style by kind. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay")
	FGameplayTag EventTag;
};

/**
 * URep_ReplayViewModel — the MVVM projection a replay scrubber widget binds to.
 *
 * Built on the engine FieldNotification system via UDP_ViewModelBase (NOT the optional MVVM
 * plugin), exactly like the other DesignPatterns view-models. It holds NO gameplay pointers in
 * the binding sense: its owner (the replay UI) pushes transport state into it via Refresh* and it
 * raises field-changed notifications so the bound view re-reads.
 *
 * It is wired to a URep_PlaybackController (transport) and a URep_ReplayTimeline (markers); the
 * owner ticks RefreshFromTransport each frame (or on a timer) to keep NormalizedPosition live, and
 * calls RebuildMarkers when the timeline changes. The view-model can also forward scrub/seek input
 * back to the controller via the Request* methods so the widget needs only this object.
 *
 * Observable fields:
 *  - NormalizedPosition : current playhead position in [0,1].
 *  - CurrentTime / TotalTime : seconds, for a time read-out.
 *  - PlaybackSpeed      : current speed (0 while paused).
 *  - bPaused            : transport paused flag.
 *  - bHasReplay         : whether a demo is actively playing (drives enabling the scrubber).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Rep Replay ViewModel"))
class DESIGNPATTERNSREPLAY_API URep_ReplayViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this view-model's observable fields. */
	enum class EField : int32
	{
		NormalizedPosition = 0,
		CurrentTime,
		TotalTime,
		PlaybackSpeed,
		bPaused,
		bHasReplay,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this view-model's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/** Bind the transport controller and timeline this view-model projects. Rebuilds markers. */
	UFUNCTION(BlueprintCallable, Category = "Rep|Replay")
	void Bind(URep_PlaybackController* InController, URep_ReplayTimeline* InTimeline);

	/** Re-read transport state from the bound controller and broadcast any changed fields. */
	UFUNCTION(BlueprintCallable, Category = "Rep|Replay")
	void RefreshFromTransport();

	/** Rebuild the marker list from the bound timeline (call when the timeline changes). */
	UFUNCTION(BlueprintCallable, Category = "Rep|Replay")
	void RebuildMarkers();

	// --- View -> controller intent (so the widget only needs this object) ---

	/** Scrub to a normalized [0,1] position; forwards to the controller's SeekToTime. */
	UFUNCTION(BlueprintCallable, Category = "Rep|Replay")
	void RequestScrubTo(float NormalizedValue);

	/** Set playback speed via the controller. */
	UFUNCTION(BlueprintCallable, Category = "Rep|Replay")
	void RequestSetSpeed(float Speed);

	/** Toggle pause via the controller. */
	UFUNCTION(BlueprintCallable, Category = "Rep|Replay")
	void RequestTogglePause();

	/** Seek to the marker at MarkerIndex (no-op if out of range). */
	UFUNCTION(BlueprintCallable, Category = "Rep|Replay")
	void RequestSeekToMarker(int32 MarkerIndex);

	// --- Observable getters ---

	/** Current playhead position in [0,1]. */
	UFUNCTION(BlueprintPure, Category = "Rep|Replay")
	float GetNormalizedPosition() const { return NormalizedPosition; }

	/** Current playback time in seconds. */
	UFUNCTION(BlueprintPure, Category = "Rep|Replay")
	float GetCurrentTime() const { return CurrentTime; }

	/** Total demo length in seconds. */
	UFUNCTION(BlueprintPure, Category = "Rep|Replay")
	float GetTotalTime() const { return TotalTime; }

	/** Current playback speed (0 while paused). */
	UFUNCTION(BlueprintPure, Category = "Rep|Replay")
	float GetPlaybackSpeed() const { return PlaybackSpeed; }

	/** True while paused. */
	UFUNCTION(BlueprintPure, Category = "Rep|Replay")
	bool IsPaused() const { return bPaused; }

	/** True while a demo is actively playing (drives enabling the scrubber). */
	UFUNCTION(BlueprintPure, Category = "Rep|Replay")
	bool HasReplay() const { return bHasReplay; }

	/** The scrubber markers, in chronological order. Not a FieldNotify field; read after RebuildMarkers. */
	UFUNCTION(BlueprintPure, Category = "Rep|Replay")
	const TArray<FRep_TimelineMarker>& GetMarkers() const { return Markers; }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Bound transport controller (weak: the owner owns lifetime, the VM only projects it). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_PlaybackController> Controller;

	/** Bound timeline (weak: same rationale). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_ReplayTimeline> Timeline;

	/** Backing storage. */
	UPROPERTY(Transient)
	float NormalizedPosition = 0.f;

	UPROPERTY(Transient)
	float CurrentTime = 0.f;

	UPROPERTY(Transient)
	float TotalTime = 0.f;

	UPROPERTY(Transient)
	float PlaybackSpeed = 1.f;

	UPROPERTY(Transient)
	bool bPaused = false;

	UPROPERTY(Transient)
	bool bHasReplay = false;

	/** Projected scrubber markers (rebuilt from the timeline). */
	UPROPERTY(Transient)
	TArray<FRep_TimelineMarker> Markers;
};
