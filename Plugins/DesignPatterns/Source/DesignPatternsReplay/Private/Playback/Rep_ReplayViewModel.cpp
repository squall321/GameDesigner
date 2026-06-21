// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Playback/Rep_ReplayViewModel.h"
#include "Playback/Rep_PlaybackController.h"
#include "Timeline/Rep_ReplayTimeline.h"
#include "Timeline/Rep_ReplayEvent.h"
#include "Core/DPLog.h"

namespace UE::FieldNotification
{
	/** Descriptor enumerating URep_ReplayViewModel's observable fields by name. */
	struct FRep_ReplayViewModelDescriptor : public IClassDescriptor
	{
		static const FName FieldNames[(int32)URep_ReplayViewModel::EField::Num];

		static FFieldId MakeId(URep_ReplayViewModel::EField Field)
		{
			const int32 Index = (int32)Field;
			return FFieldId(FieldNames[Index], Index);
		}

		virtual void ForEachField(const UClass* /*Class*/, TFunctionRef<bool(FFieldId)> Callback) const override
		{
			for (int32 Index = 0; Index < (int32)URep_ReplayViewModel::EField::Num; ++Index)
			{
				if (!Callback(FFieldId(FieldNames[Index], Index)))
				{
					break;
				}
			}
		}
	};

	const FName FRep_ReplayViewModelDescriptor::FieldNames[(int32)URep_ReplayViewModel::EField::Num] =
	{
		FName(TEXT("NormalizedPosition")),
		FName(TEXT("CurrentTime")),
		FName(TEXT("TotalTime")),
		FName(TEXT("PlaybackSpeed")),
		FName(TEXT("bPaused")),
		FName(TEXT("bHasReplay")),
	};

	static const FRep_ReplayViewModelDescriptor GRep_ReplayViewModelDescriptor;
}

const UE::FieldNotification::IClassDescriptor& URep_ReplayViewModel::GetFieldNotificationDescriptor() const
{
	return UE::FieldNotification::GRep_ReplayViewModelDescriptor;
}

UE::FieldNotification::FFieldId URep_ReplayViewModel::GetFieldId(EField Field)
{
	return UE::FieldNotification::FRep_ReplayViewModelDescriptor::MakeId(Field);
}

void URep_ReplayViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

void URep_ReplayViewModel::Bind(URep_PlaybackController* InController, URep_ReplayTimeline* InTimeline)
{
	Controller = InController;
	Timeline = InTimeline;
	RebuildMarkers();
	RefreshFromTransport();
}

void URep_ReplayViewModel::RefreshFromTransport()
{
	URep_PlaybackController* C = Controller.Get();

	const bool bNewHasReplay = (C != nullptr) && C->IsPlaybackActive();
	const float NewTotal = C ? C->GetTotalTime() : 0.f;
	const float NewCurrent = C ? C->GetCurrentTime() : 0.f;
	const float NewNorm = C ? C->GetNormalizedPosition() : 0.f;
	const float NewSpeed = C ? C->GetPlaybackSpeed() : 0.f;
	const bool bNewPaused = C ? C->IsPaused() : false;

	if (!FMath::IsNearlyEqual(NormalizedPosition, NewNorm))
	{
		NormalizedPosition = NewNorm;
		BroadcastField(EField::NormalizedPosition);
	}
	if (!FMath::IsNearlyEqual(CurrentTime, NewCurrent))
	{
		CurrentTime = NewCurrent;
		BroadcastField(EField::CurrentTime);
	}
	if (!FMath::IsNearlyEqual(TotalTime, NewTotal))
	{
		TotalTime = NewTotal;
		BroadcastField(EField::TotalTime);
		// Total length changing means marker normalized positions need recomputing.
		RebuildMarkers();
	}
	if (!FMath::IsNearlyEqual(PlaybackSpeed, NewSpeed))
	{
		PlaybackSpeed = NewSpeed;
		BroadcastField(EField::PlaybackSpeed);
	}
	if (bPaused != bNewPaused)
	{
		bPaused = bNewPaused;
		BroadcastField(EField::bPaused);
	}
	if (bHasReplay != bNewHasReplay)
	{
		bHasReplay = bNewHasReplay;
		BroadcastField(EField::bHasReplay);
	}
}

void URep_ReplayViewModel::RebuildMarkers()
{
	Markers.Reset();

	const URep_ReplayTimeline* T = Timeline.Get();
	if (!T)
	{
		return;
	}

	// Normalize against the known total length; if unknown yet, fall back to the last event time so
	// markers still spread sensibly (a documented defensive fallback rather than collapsing to 0).
	float Span = TotalTime;
	const TArray<FRep_ReplayEvent>& Events = T->GetEvents();
	if (Span <= KINDA_SMALL_NUMBER && Events.Num() > 0)
	{
		Span = Events.Last().Time;
	}

	Markers.Reserve(Events.Num());
	for (const FRep_ReplayEvent& E : Events)
	{
		FRep_TimelineMarker Marker;
		Marker.TimeSeconds = E.Time;
		Marker.NormalizedPosition = (Span > KINDA_SMALL_NUMBER) ? FMath::Clamp(E.Time / Span, 0.f, 1.f) : 0.f;
		Marker.Label = E.GetEffectiveLabel();
		Marker.EventTag = E.EventTag;
		Markers.Add(MoveTemp(Marker));
	}
}

void URep_ReplayViewModel::RequestScrubTo(float NormalizedValue)
{
	if (URep_PlaybackController* C = Controller.Get())
	{
		const float Total = C->GetTotalTime();
		C->SeekToTime(FMath::Clamp(NormalizedValue, 0.f, 1.f) * Total);
	}
}

void URep_ReplayViewModel::RequestSetSpeed(float Speed)
{
	if (URep_PlaybackController* C = Controller.Get())
	{
		C->SetPlaybackSpeed(Speed);
		RefreshFromTransport();
	}
}

void URep_ReplayViewModel::RequestTogglePause()
{
	if (URep_PlaybackController* C = Controller.Get())
	{
		C->TogglePause();
		RefreshFromTransport();
	}
}

void URep_ReplayViewModel::RequestSeekToMarker(int32 MarkerIndex)
{
	if (!Markers.IsValidIndex(MarkerIndex))
	{
		return;
	}
	URep_PlaybackController* C = Controller.Get();
	const URep_ReplayTimeline* T = Timeline.Get();
	if (!C || !T)
	{
		return;
	}

	// Re-resolve the matching event so SeekToEvent applies the configured lead-in.
	const float MarkerTime = Markers[MarkerIndex].TimeSeconds;
	FRep_ReplayEvent Event;
	if (T->FindNextEvent(MarkerTime - KINDA_SMALL_NUMBER, FGameplayTag(), Event))
	{
		C->SeekToEvent(Event);
	}
	else
	{
		C->SeekToTime(MarkerTime);
	}
}
