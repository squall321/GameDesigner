// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/IClassDescriptor.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "UObject/WeakInterfacePtr.h"
#include "Rep_TelemetryViewModel.generated.h"

class URep_PlaybackController;
class ISeam_AnalyticsSink;

// ---- Data types ---------------------------------------------------------------

/**
 * A single telemetry metric sample displayed in the replay telemetry overlay.
 *
 * Flat and copyable (FSeam_NetValue + FGameplayTag + FText) — no UObject refs — so this struct is safe to
 * push from the view-model to a widget without additional lifetime management.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_TelemetrySample
{
	GENERATED_BODY()

	/** The metric identity tag (e.g. Rep.Analytics.Perf.FrameMs, Rep.Analytics.Highlight.Count). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Telemetry")
	FGameplayTag MetricTag;

	/** A short human-readable display name for the metric (shown as the row label in the overlay). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Telemetry")
	FText DisplayName;

	/** The current value of the metric; the variant type determines how the overlay formats it. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Telemetry")
	FSeam_NetValue Value;
};

// ---- Delegates ----------------------------------------------------------------

/** Fired when the overlay enabled state changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRep_OnTelemetryOverlayToggled, bool, bEnabled);

// ---- URep_TelemetryViewModel --------------------------------------------------

/**
 * URep_TelemetryViewModel — the MVVM projection for the telemetry overlay displayed during replay playback.
 *
 * ARCHITECTURE:
 *  - A SIBLING view-model to URep_ReplayViewModel. It does NOT subclass it; it has its OWN independent
 *    EField enum, IClassDescriptor implementation, and GetFieldId so it does not collide with
 *    URep_ReplayViewModel's field ids. This is the pattern mandated by the spec and mirrors
 *    ULoc_SubtitleHistoryViewModel's relationship with ULoc_SubtitleViewModel.
 *  - Owner (the replay UI) calls RefreshFromPlayback(DeltaSeconds) each frame while the overlay is
 *    enabled; the view-model re-reads the metrics it tracks and broadcasts field changes.
 *  - It does NOT replicate, record, or mutate any authoritative state. It is purely a read + project
 *    layer, local and cosmetic.
 *
 * METRICS TRACKED:
 *  - Playback time / speed / pause state (sourced from the bound URep_PlaybackController, same transport
 *    the main scrubber uses — wired separately so the telemetry overlay can exist without the main VM).
 *  - Per-frame delta-time (milliseconds) sampled from the engine (FApp::GetDeltaTime) during playback to
 *    show demo-playback frame budget.
 *  - An optional analytics sink metric snapshot: if the ISeam_AnalyticsSink is live AND the setting
 *    bForwardHighlightsToAnalytics is enabled, the overlay can request a snapshot of aggregate counts
 *    (highlight detected count, session frame budget percentiles) from the Analytics module. When the sink
 *    is absent the analytics rows are simply absent — the overlay degrades gracefully, no-op.
 *
 * LIFECYCLE / GC:
 *  - Owned by the replay UI host (a NewObject or instanced subobject). Not a subsystem.
 *  - Holds the playback controller WEAKLY (via TWeakObjectPtr UPROPERTY Transient), the analytics sink
 *    weakly (TWeakInterfacePtr), re-resolved on each refresh. No cross-world strong refs.
 *  - The analytics sink is optional and gated by settings; if unresolved the analytics rows are absent.
 *
 * MP CAVEAT: the telemetry overlay shows local-machine playback metrics. During a demo replay these reflect
 * the client's playback performance (frame time on this machine), NOT the server's original recording
 * performance. This is documented cosmetic behavior.
 *
 * Observable fields (broadcast via BroadcastFieldValueChanged when they change):
 *  - PlaybackTimeSeconds   : current demo time (from the controller).
 *  - PlaybackSpeed         : current playback speed / dilation.
 *  - bIsPaused             : transport paused flag.
 *  - FrameDeltaMs          : this frame's delta in milliseconds (sampled during refresh).
 *  - Samples               : TArray<FRep_TelemetrySample> — the current full metric snapshot (rebuilt
 *                            when any tracked metric changes by more than a small epsilon).
 *  - bOverlayEnabled       : master on/off for the overlay (controls widget visibility).
 */
UCLASS(BlueprintType, meta = (DisplayName = "Rep Telemetry Overlay ViewModel"))
class DESIGNPATTERNSREPLAY_API URep_TelemetryViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this view-model's observable fields. */
	enum class EField : int32
	{
		PlaybackTimeSeconds = 0,
		PlaybackSpeed,
		bIsPaused,
		FrameDeltaMs,
		Samples,
		bOverlayEnabled,
		/** Sentinel — always last. */
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this view-model's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	// ---- Binding --------------------------------------------------------------

	/**
	 * Bind the playback controller this view-model reads transport state from. Re-bindable; passing null
	 * unbinds. The controller is held WEAKLY so the VM does not prevent it from being GC'd.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Telemetry")
	void BindPlaybackController(URep_PlaybackController* InController);

	// ---- Update --------------------------------------------------------------

	/**
	 * Re-read transport + frame metrics, optionally poll the analytics sink for an aggregate snapshot,
	 * and broadcast any changed fields. Call once per frame while the overlay is enabled.
	 *
	 * @param DeltaSeconds  The game-thread frame delta (seconds); used to compute FrameDeltaMs.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Telemetry")
	void RefreshFromPlayback(float DeltaSeconds);

	/**
	 * Register an additional metric the overlay should display. Metrics registered here appear in the
	 * Samples array after the next RefreshFromPlayback. Calling with an already-registered MetricTag
	 * updates the DisplayName. Registering with an invalid tag is a no-op.
	 *
	 * This lets the host or the analytics layer push domain-specific metrics (e.g. highlight count,
	 * session drop-off) into the telemetry overlay without this module knowing their concrete type.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Telemetry")
	void RegisterMetric(FGameplayTag MetricTag, const FText& DisplayName, const FSeam_NetValue& InitialValue);

	/** Push an updated value for a previously-registered metric. No-op if the tag is not registered. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Telemetry")
	void UpdateMetric(FGameplayTag MetricTag, const FSeam_NetValue& NewValue);

	/** Remove a metric from the overlay (unregisters it from future sample snapshots). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Telemetry")
	void UnregisterMetric(FGameplayTag MetricTag);

	// ---- Overlay toggle -------------------------------------------------------

	/**
	 * Enable or disable the telemetry overlay. Broadcasts the bOverlayEnabled field change and
	 * OnTelemetryOverlayToggled. When disabled, RefreshFromPlayback is a no-op (saves per-frame cost).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Telemetry")
	void SetOverlayEnabled(bool bEnabled);

	/** Toggle the overlay on/off. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Telemetry")
	void ToggleOverlay();

	// ---- Getters (for bound widget) -------------------------------------------

	/** Current playback time in seconds (from the controller). 0 when not bound or not playing. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Replay|Telemetry")
	float GetPlaybackTimeSeconds() const { return PlaybackTimeSeconds; }

	/** Current playback speed (0 while paused). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Replay|Telemetry")
	float GetPlaybackSpeed() const { return PlaybackSpeed; }

	/** True while the demo is paused. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Replay|Telemetry")
	bool IsPaused() const { return bIsPaused; }

	/** Last sampled frame delta in milliseconds (updated each RefreshFromPlayback). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Replay|Telemetry")
	float GetFrameDeltaMs() const { return FrameDeltaMs; }

	/** The current metric snapshot (rebuilt when any metric changes on RefreshFromPlayback). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Replay|Telemetry")
	const TArray<FRep_TelemetrySample>& GetSamples() const { return Samples; }

	/** True while the overlay is enabled. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Replay|Telemetry")
	bool IsOverlayEnabled() const { return bOverlayEnabled; }

	// ---- Delegates ------------------------------------------------------------

	/** Broadcast when the overlay is toggled on or off. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Telemetry")
	FRep_OnTelemetryOverlayToggled OnTelemetryOverlayToggled;

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	// ---- Backing storage -------------------------------------------------------

	/** Bound playback controller (weak — the host owns it). */
	UPROPERTY(Transient)
	TWeakObjectPtr<URep_PlaybackController> Controller;

	/** Analytics sink (optional; weak + pruned-on-use). Resolved from the locator each refresh when stale. */
	TWeakInterfacePtr<ISeam_AnalyticsSink> AnalyticsSink;

	/** Current demo playback time (seconds). */
	UPROPERTY(Transient)
	float PlaybackTimeSeconds = 0.f;

	/** Current playback speed (0 while paused). */
	UPROPERTY(Transient)
	float PlaybackSpeed = 1.f;

	/** True while paused. */
	UPROPERTY(Transient)
	bool bIsPaused = false;

	/** Last frame delta in milliseconds. */
	UPROPERTY(Transient)
	float FrameDeltaMs = 0.f;

	/** The current full metric snapshot (projected to the UI). */
	UPROPERTY(Transient)
	TArray<FRep_TelemetrySample> Samples;

	/** True while the overlay is on. */
	UPROPERTY(Transient)
	bool bOverlayEnabled = false;

	// ---- Host-registered metric store ----------------------------------------

	/** Host- and analytics-registered additional metrics, keyed by MetricTag for O(1) update. */
	TMap<FGameplayTag, FRep_TelemetrySample> RegisteredMetrics;

	// ---- Analytics sink helpers ----------------------------------------------

	/**
	 * Attempt to resolve the ISeam_AnalyticsSink from the service locator (using the service tag from
	 * URep_DeveloperSettings::AnalyticsSinkServiceTag). Returns null when absent or not ready.
	 * Re-resolves each call if the cached weak ptr is dead (pruned-on-use pattern).
	 */
	ISeam_AnalyticsSink* ResolveAnalyticsSink();

	// ---- Rebuild helpers -----------------------------------------------------

	/**
	 * Rebuild the Samples array from the transport state + registered metrics (+ optional analytics
	 * snapshot) and broadcast the Samples field change if the content differs. Called from
	 * RefreshFromPlayback after transport fields have been updated.
	 */
	void RebuildSamples();

	/**
	 * Compare two sample arrays for significant difference (tag set change or value drift above epsilon).
	 * Used to suppress the Samples broadcast when nothing meaningful changed.
	 */
	static bool SamplesAreDifferent(const TArray<FRep_TelemetrySample>& A, const TArray<FRep_TelemetrySample>& B);
};
