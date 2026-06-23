// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Telemetry/Rep_TelemetryViewModel.h"
#include "Playback/Rep_PlaybackController.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "DesignPatternsReplayModule.h"

#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Analytics/Seam_AnalyticsSink.h"
#include "FieldNotification/FieldNotificationClassDescriptor.h"
#include "FieldNotification/FieldId.h"

#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"

// ---------------------------------------------------------------------------
// FieldNotification descriptor
//
// One static FFieldNotificationClassDescriptor (engine pattern) per concrete VM class.
// EField must NOT overlap with any parent-class field ids — this class uses its OWN enum starting at 0.
// ---------------------------------------------------------------------------

namespace
{
	// The descriptor is a static const array mapping EField ordinals to FFieldId objects.
	// Matches the EField enum 1:1.
	static const UE::FieldNotification::FFieldId GFieldIds[static_cast<int32>(URep_TelemetryViewModel::EField::Num)]
	{
		/* PlaybackTimeSeconds */ UE::FieldNotification::FFieldId(TEXT("PlaybackTimeSeconds"), 0),
		/* PlaybackSpeed       */ UE::FieldNotification::FFieldId(TEXT("PlaybackSpeed"),       1),
		/* bIsPaused           */ UE::FieldNotification::FFieldId(TEXT("bIsPaused"),           2),
		/* FrameDeltaMs        */ UE::FieldNotification::FFieldId(TEXT("FrameDeltaMs"),        3),
		/* Samples             */ UE::FieldNotification::FFieldId(TEXT("Samples"),             4),
		/* bOverlayEnabled     */ UE::FieldNotification::FFieldId(TEXT("bOverlayEnabled"),     5),
	};

	/** A minimal class descriptor for this view-model. */
	class FTelemetryVMClassDescriptor final : public UE::FieldNotification::IClassDescriptor
	{
	public:
		virtual void ForEachField(const UClass* Class,
			TFunctionRef<bool(UE::FieldNotification::FFieldId)> Callback) const override
		{
			for (const UE::FieldNotification::FFieldId& Id : GFieldIds)
			{
				if (!Callback(Id))
				{
					return;
				}
			}
		}
	};

	static FTelemetryVMClassDescriptor GTelemetryVMDescriptor;
}

// ---------------------------------------------------------------------------
// INotifyFieldValueChanged
// ---------------------------------------------------------------------------

const UE::FieldNotification::IClassDescriptor& URep_TelemetryViewModel::GetFieldNotificationDescriptor() const
{
	return GTelemetryVMDescriptor;
}

UE::FieldNotification::FFieldId URep_TelemetryViewModel::GetFieldId(EField Field)
{
	const int32 Idx = static_cast<int32>(Field);
	if (Idx >= 0 && Idx < static_cast<int32>(EField::Num))
	{
		return GFieldIds[Idx];
	}
	ensureMsgf(false, TEXT("URep_TelemetryViewModel::GetFieldId: invalid field %d."), Idx);
	return {};
}

// ---------------------------------------------------------------------------
// BroadcastField helper
// ---------------------------------------------------------------------------

void URep_TelemetryViewModel::BroadcastField(EField Field)
{
	BroadcastFieldValueChanged(GetFieldId(Field));
}

// ---------------------------------------------------------------------------
// Binding
// ---------------------------------------------------------------------------

void URep_TelemetryViewModel::BindPlaybackController(URep_PlaybackController* InController)
{
	Controller = InController;

	// Reset displayed transport values on rebind so stale values are not shown.
	PlaybackTimeSeconds = 0.f;
	PlaybackSpeed       = 1.f;
	bIsPaused           = false;
	BroadcastField(EField::PlaybackTimeSeconds);
	BroadcastField(EField::PlaybackSpeed);
	BroadcastField(EField::bIsPaused);
}

// ---------------------------------------------------------------------------
// RefreshFromPlayback
// ---------------------------------------------------------------------------

void URep_TelemetryViewModel::RefreshFromPlayback(float DeltaSeconds)
{
	if (!bOverlayEnabled)
	{
		// When the overlay is off, skip all per-frame metric reads.
		return;
	}

	bool bTransportDirty = false;

	// --- Transport state ---
	if (URep_PlaybackController* Ctrl = Controller.Get())
	{
		const float NewTime  = Ctrl->GetCurrentTime();
		const float NewSpeed = Ctrl->GetPlaybackSpeed();
		const bool  NewPaused= Ctrl->IsPaused();

		if (!FMath::IsNearlyEqual(PlaybackTimeSeconds, NewTime, 0.01f))
		{
			PlaybackTimeSeconds = NewTime;
			BroadcastField(EField::PlaybackTimeSeconds);
		}
		if (!FMath::IsNearlyEqual(PlaybackSpeed, NewSpeed, 0.001f))
		{
			PlaybackSpeed = NewSpeed;
			BroadcastField(EField::PlaybackSpeed);
			bTransportDirty = true;
		}
		if (bIsPaused != NewPaused)
		{
			bIsPaused = NewPaused;
			BroadcastField(EField::bIsPaused);
			bTransportDirty = true;
		}
	}

	// --- Frame delta ---
	{
		// FApp::GetDeltaTime returns the raw game-thread delta in seconds.
		const float NewDeltaMs = static_cast<float>(FApp::GetDeltaTime() * 1000.0);
		if (!FMath::IsNearlyEqual(FrameDeltaMs, NewDeltaMs, 0.1f))
		{
			FrameDeltaMs = NewDeltaMs;
			BroadcastField(EField::FrameDeltaMs);
			bTransportDirty = true;
		}
	}

	// --- Rebuild sample list when something changed ---
	if (bTransportDirty)
	{
		RebuildSamples();
	}
}

// ---------------------------------------------------------------------------
// Metric registration
// ---------------------------------------------------------------------------

void URep_TelemetryViewModel::RegisterMetric(FGameplayTag MetricTag, const FText& DisplayName,
	const FSeam_NetValue& InitialValue)
{
	if (!MetricTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("TelemetryViewModel: RegisterMetric called with invalid tag; ignored."));
		return;
	}

	FRep_TelemetrySample& Sample = RegisteredMetrics.FindOrAdd(MetricTag);
	Sample.MetricTag    = MetricTag;
	Sample.DisplayName  = DisplayName;
	Sample.Value        = InitialValue;

	RebuildSamples();
}

void URep_TelemetryViewModel::UpdateMetric(FGameplayTag MetricTag, const FSeam_NetValue& NewValue)
{
	if (!MetricTag.IsValid())
	{
		return;
	}

	FRep_TelemetrySample* Sample = RegisteredMetrics.Find(MetricTag);
	if (!Sample)
	{
		return;
	}

	// Only broadcast if the value actually changed (suppresses unnecessary UI churn).
	// For numeric variants check approximate equality; for others do a type+raw comparison.
	bool bChanged = (Sample->Value.Type != NewValue.Type);
	if (!bChanged)
	{
		switch (NewValue.Type)
		{
		case ESeam_NetValueType::Int:
			bChanged = (Sample->Value.IntValue != NewValue.IntValue);
			break;
		case ESeam_NetValueType::Float:
			bChanged = !FMath::IsNearlyEqual(Sample->Value.FloatValue, NewValue.FloatValue, 0.001);
			break;
		case ESeam_NetValueType::Bool:
			bChanged = (Sample->Value.bValue != NewValue.bValue);
			break;
		default:
			// For Tag/Name/Vector always treat as changed (structural comparison is cheap enough).
			bChanged = true;
			break;
		}
	}

	if (bChanged)
	{
		Sample->Value = NewValue;
		RebuildSamples();
	}
}

void URep_TelemetryViewModel::UnregisterMetric(FGameplayTag MetricTag)
{
	if (RegisteredMetrics.Remove(MetricTag) > 0)
	{
		RebuildSamples();
	}
}

// ---------------------------------------------------------------------------
// Overlay toggle
// ---------------------------------------------------------------------------

void URep_TelemetryViewModel::SetOverlayEnabled(bool bEnabled)
{
	if (bOverlayEnabled == bEnabled)
	{
		return;
	}
	bOverlayEnabled = bEnabled;
	BroadcastField(EField::bOverlayEnabled);
	OnTelemetryOverlayToggled.Broadcast(bEnabled);
}

void URep_TelemetryViewModel::ToggleOverlay()
{
	SetOverlayEnabled(!bOverlayEnabled);
}

// ---------------------------------------------------------------------------
// Analytics sink resolution
// ---------------------------------------------------------------------------

ISeam_AnalyticsSink* URep_TelemetryViewModel::ResolveAnalyticsSink()
{
	// Pruned-on-use: return the cached weak interface if still live.
	if (AnalyticsSink.IsValid())
	{
		return AnalyticsSink.Get();
	}
	AnalyticsSink.Reset();

	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	if (!Settings || !Settings->AnalyticsSinkServiceTag.IsValid())
	{
		// Analytics forwarding is not configured; silently absent — not an error.
		return nullptr;
	}
	if (!Settings->bForwardHighlightsToAnalytics)
	{
		// Analytics sink intentionally disabled for this project.
		return nullptr;
	}

	// Need a game instance to reach the service locator. This VM is a plain UObject, so we walk
	// the outer chain to find a world / game-instance context.
	UObject* Outer = GetOuter();
	UWorld* World  = nullptr;
	while (Outer)
	{
		if (UWorld* W = Cast<UWorld>(Outer))
		{
			World = W;
			break;
		}
		Outer = Outer->GetOuter();
	}
	if (!World && GEngine)
	{
		// Fall back to the first available world (typically the only one in shipping).
		for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
		{
			if (Ctx.World())
			{
				World = Ctx.World();
				break;
			}
		}
	}
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GI = World->GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}
	UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	if (!Locator)
	{
		return nullptr;
	}

	UObject* Provider = Locator->ResolveService(Settings->AnalyticsSinkServiceTag);
	if (Provider && Provider->GetClass()->ImplementsInterface(USeam_AnalyticsSink::StaticClass()))
	{
		AnalyticsSink = TWeakInterfacePtr<ISeam_AnalyticsSink>(Provider);
		if (AnalyticsSink.IsValid())
		{
			return AnalyticsSink.Get();
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Sample rebuild
// ---------------------------------------------------------------------------

void URep_TelemetryViewModel::RebuildSamples()
{
	TArray<FRep_TelemetrySample> NewSamples;
	NewSamples.Reserve(2 + RegisteredMetrics.Num());

	// --- Built-in transport metrics ---
	{
		FRep_TelemetrySample S;
		S.MetricTag   = FGameplayTag::RequestGameplayTag(FName("Rep.Telemetry.Playback.Speed"), false);
		S.DisplayName = NSLOCTEXT("Replay", "Telemetry_Speed", "Playback Speed");
		S.Value       = FSeam_NetValue::MakeFloat(bIsPaused ? 0.0 : static_cast<double>(PlaybackSpeed));
		NewSamples.Add(S);
	}
	{
		FRep_TelemetrySample S;
		S.MetricTag   = FGameplayTag::RequestGameplayTag(FName("Rep.Telemetry.Playback.FrameMs"), false);
		S.DisplayName = NSLOCTEXT("Replay", "Telemetry_FrameMs", "Frame (ms)");
		S.Value       = FSeam_NetValue::MakeFloat(static_cast<double>(FrameDeltaMs));
		NewSamples.Add(S);
	}

	// --- Optional analytics sink snapshot (PII-safe aggregate data only) ---
	ISeam_AnalyticsSink* Sink = ResolveAnalyticsSink();
	if (Sink)
	{
		UObject* SinkObj = AnalyticsSink.GetObject();
		if (SinkObj && ISeam_AnalyticsSink::Execute_IsSinkReady(SinkObj))
		{
			// The analytics module does not expose a "get snapshot" API via the seam — the seam is
			// write-only (RecordAggregateEvent). To show analytics-derived metrics, the analytics module
			// (or the host) registers them via RegisterMetric/UpdateMetric. This code block is the hook
			// for future extension: the host can call RegisterMetric with analytics tags between sessions.
			// Here we simply emit a "Sink Ready" indicator metric so the overlay shows the connection.
			FRep_TelemetrySample S;
			S.MetricTag   = FGameplayTag::RequestGameplayTag(FName("Rep.Telemetry.Analytics.SinkReady"), false);
			S.DisplayName = NSLOCTEXT("Replay", "Telemetry_SinkReady", "Analytics Sink");
			S.Value       = FSeam_NetValue::MakeBool(true);
			NewSamples.Add(S);
		}
	}

	// --- Host- and analytics-pushed metrics ---
	for (const TPair<FGameplayTag, FRep_TelemetrySample>& Pair : RegisteredMetrics)
	{
		NewSamples.Add(Pair.Value);
	}

	// Broadcast only if the sample set actually changed (avoids O(n) widget refreshes each frame).
	if (SamplesAreDifferent(Samples, NewSamples))
	{
		Samples = MoveTemp(NewSamples);
		BroadcastField(EField::Samples);
	}
}

bool URep_TelemetryViewModel::SamplesAreDifferent(const TArray<FRep_TelemetrySample>& A,
	const TArray<FRep_TelemetrySample>& B)
{
	if (A.Num() != B.Num())
	{
		return true;
	}
	for (int32 I = 0; I < A.Num(); ++I)
	{
		if (A[I].MetricTag != B[I].MetricTag)
		{
			return true;
		}
		// Coarse value comparison: type mismatch is always different.
		if (A[I].Value.Type != B[I].Value.Type)
		{
			return true;
		}
		switch (A[I].Value.Type)
		{
		case ESeam_NetValueType::Int:
			if (A[I].Value.IntValue != B[I].Value.IntValue) return true;
			break;
		case ESeam_NetValueType::Float:
			if (!FMath::IsNearlyEqual(A[I].Value.FloatValue, B[I].Value.FloatValue, 0.01)) return true;
			break;
		case ESeam_NetValueType::Bool:
			if (A[I].Value.bValue != B[I].Value.bValue) return true;
			break;
		default:
			// For Tag/Name/Vector treat as always-different to ensure re-broadcast (cheap for small N).
			return true;
		}
	}
	return false;
}
