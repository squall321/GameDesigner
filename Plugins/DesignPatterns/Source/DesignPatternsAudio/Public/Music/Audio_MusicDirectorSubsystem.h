// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Containers/Ticker.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/WeakInterfacePtr.h"
#include "MessageBus/DPMessage.h"
#include "Music/Audio_MusicQuantize.h"
#include "Audio_MusicDirectorSubsystem.generated.h"

class UAudioComponent;
class USoundBase;
class UAudio_MusicStateDataAsset;
class UAudio_MusicEventMapDataAsset;
class UQuartzClockHandle;
class ISeam_SimClock;
struct FAudio_MusicEventRule;

/**
 * One live looping voice playing a single music-state layer.
 *
 * The director keeps a small pool of these. Each wraps a persistent UAudioComponent (kept alive by
 * UPROPERTY so GC doesn't reclaim a playing voice) and tracks the volume crossfade from a start
 * value to a target value over a duration, advanced every frame by the FTSTicker.
 */
USTRUCT()
struct FAudio_MusicVoice
{
	GENERATED_BODY()

	/** Persistent audio component for this voice. Owned (UPROPERTY) so it survives GC while playing. */
	UPROPERTY()
	TObjectPtr<UAudioComponent> Component = nullptr;

	/** The state-layer's base volume target this voice is fading toward (already folds in layer/state volume). */
	float TargetVolume = 0.0f;

	/** Volume the current fade started from. */
	float StartVolume = 0.0f;

	/** Volume right now (drives the component each tick). */
	float CurrentVolume = 0.0f;

	/** Seconds elapsed in the current fade. */
	float FadeElapsed = 0.0f;

	/** Total seconds of the current fade (0 = snap). */
	float FadeDuration = 0.0f;

	/** True once a fade-out has driven this voice to silence and it should be stopped/recycled. */
	bool bRetiring = false;

	/** Index into the active state's Layers array this voice is rendering (INDEX_NONE if free). */
	int32 LayerIndex = INDEX_NONE;

	/** Begin a fade from CurrentVolume to NewTarget over Duration seconds. */
	void StartFade(float NewTarget, float Duration);

	/** Advance the fade by DeltaTime, updating CurrentVolume. Returns true if the fade finished. */
	bool Advance(float DeltaTime);
};

/**
 * Adaptive / layered MUSIC DIRECTOR — a small tag-keyed music state model on top of the engine's
 * audio components.
 *
 * MODEL
 *   - The director holds at most one ACTIVE music state (a UAudio_MusicStateDataAsset addressed by
 *     tag). A state is a set of synchronized looping stems ("layers"); the director plays them all
 *     phase-locked and mixes their volumes by a single normalized INTENSITY scalar (vertical
 *     re-orchestration). SetIntensity(float) re-targets every layer's volume each frame.
 *   - SetMusicState(Tag) CROSSFADES: the outgoing state's voices fade out while the incoming
 *     state's layers fade in, over the incoming state's CrossfadeSeconds. Horizontal re-sequencing.
 *   - TriggerStinger(Tag) plays a non-looping one-shot accent over the current bed (UGameplayStatics
 *     2D sound) without disturbing the running layers.
 *
 * REACTIVITY (decoupled)
 *   - The director installs a UAudio_MusicEventMapDataAsset and SUBSCRIBES to the message bus
 *     (ListenNative) on every bus channel that map references. Mapped events drive SetMusicState /
 *     TriggerStinger / SetIntensity. The mapping is pure tag data, so the audio module NEVER hard-
 *     includes Combat (or any sibling) — gameplay only broadcasts DP.Bus.* anchors.
 *
 * SCOPE: GameInstance-scoped, LOCAL and COSMETIC. Nothing here is replicated; it is driven by
 *   already-replicated gameplay surfaced on the bus, so it runs identically on every client.
 *
 * TICKING: the director is NOT an FTickableGameObject; it drains volume crossfades via an FTSTicker
 *   registered in Initialize and removed in Deinitialize, mirroring the message-bus convention and
 *   avoiding editor / seamless-travel ticking.
 */
UCLASS()
class DESIGNPATTERNSAUDIO_API UAudio_MusicDirectorSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Event-map installation ----

	/**
	 * Install (or replace) the event map that drives bus->music reactions. Resubscribes the director
	 * to the bus channels referenced by the new map and, if the map has a DefaultStateTag, transitions
	 * to it. Passing null clears the map and unsubscribes. Safe to call any time at runtime.
	 */
	UFUNCTION(BlueprintCallable, Category = "Music|Director")
	void InstallEventMap(UAudio_MusicEventMapDataAsset* EventMap);

	/** The currently-installed event map (may be null). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music|Director")
	UAudio_MusicEventMapDataAsset* GetEventMap() const { return EventMap.Get(); }

	// ---- Core music API ----

	/**
	 * Crossfade to the music state identified by StateTag. Resolves the state from the installed
	 * map's playlist first, then the core data registry. No-op (logged) if the tag can't be resolved
	 * or already active. The incoming state's CrossfadeSeconds governs the transition.
	 */
	UFUNCTION(BlueprintCallable, Category = "Music|Director")
	void SetMusicState(FGameplayTag StateTag);

	/** Crossfade directly to an already-resolved state asset (bypasses tag resolution). */
	void SetMusicStateAsset(UAudio_MusicStateDataAsset* State);

	/** Fade the current music fully out over FadeSeconds and clear the active state. */
	UFUNCTION(BlueprintCallable, Category = "Music|Director")
	void StopMusic(float FadeSeconds = 1.0f);

	/**
	 * Play a stinger one-shot (keyed by StingerTag) from the active state's stinger map. No-op if no
	 * state is active or the tag isn't present. Stingers do not affect the running layer mix.
	 */
	UFUNCTION(BlueprintCallable, Category = "Music|Director")
	void TriggerStinger(FGameplayTag StingerTag);

	/**
	 * Set the normalized [0,1] adaptive intensity. The director eases toward this target (governed
	 * by settings IntensityInterpSpeed) and re-mixes every active layer's volume accordingly.
	 */
	UFUNCTION(BlueprintCallable, Category = "Music|Director")
	void SetIntensity(float NewIntensity);

	/** The tag of the currently-active music state (invalid if none). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music|Director")
	FGameplayTag GetActiveStateTag() const { return ActiveStateTag; }

	/** Current eased intensity in [0,1]. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music|Director")
	float GetIntensity() const { return CurrentIntensity; }

	// ----------------------------------------------------------------------------------------------
	//  MUSIC DEPTH (6) ADDITIVE: bar-synced (quantized) transitions + optional Quartz clock.
	//  All NEW; the shipped API above is unchanged. Without tempo metadata or a Quartz clock these
	//  fall back to the existing immediate transition, so default content behaves identically.
	// ----------------------------------------------------------------------------------------------

	/**
	 * Crossfade to StateTag, but DEFER the transition to the chosen musical boundary so a horizontal
	 * re-sequence lands on the beat/bar/phrase. Immediate behaves exactly like SetMusicState. The
	 * pending request supersedes any earlier still-pending request.
	 */
	UFUNCTION(BlueprintCallable, Category = "Music|Director")
	void SetMusicStateQuantized(FGameplayTag StateTag, EAudio_MusicQuantize Quantize);

	/** Set the adaptive intensity, deferred to the chosen musical boundary (Immediate == SetIntensity). */
	UFUNCTION(BlueprintCallable, Category = "Music|Director")
	void SetIntensityQuantized(float Intensity, EAudio_MusicQuantize Quantize);

	/**
	 * Provide a caller-owned Quartz clock handle so quantized transitions snap to the engine Quartz
	 * metronome instead of the internal tempo accumulator. Pass null to detach and use the fallback.
	 * The director NEVER creates or owns a Quartz clock; it only observes one a project supplies.
	 */
	UFUNCTION(BlueprintCallable, Category = "Music|Director")
	void SetQuartzClock(UQuartzClockHandle* Clock);

	/** Provide the simulation clock seam so the fallback tempo phase respects pause / time-dilation. */
	UFUNCTION(BlueprintCallable, Category = "Music|Director")
	void SetSimClock(const TScriptInterface<ISeam_SimClock>& InClock);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

protected:
	/** Only create for game / PIE game instances (not editor utility / commandlet contexts). */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

private:
	// ---- State ----

	/** The installed event map (weak: the asset is owned by the registry/loader, not by us). */
	UPROPERTY(Transient)
	TWeakObjectPtr<UAudio_MusicEventMapDataAsset> EventMap;

	/** Strong ref to the active state asset so its soft stems can be resolved while it plays. */
	UPROPERTY(Transient)
	TObjectPtr<UAudio_MusicStateDataAsset> ActiveState = nullptr;

	/** Tag of the active state (mirror of ActiveState->DataTag, kept for fast queries). */
	UPROPERTY(Transient)
	FGameplayTag ActiveStateTag;

	/** The pool of live looping voices (active-state layers plus any fading-out previous voices). */
	UPROPERTY(Transient)
	TArray<FAudio_MusicVoice> Voices;

	/** Target adaptive intensity requested by the last SetIntensity call. */
	float TargetIntensity = 0.0f;

	/** Current eased intensity (interpolated toward TargetIntensity each tick). */
	float CurrentIntensity = 0.0f;

	/** Listener handles for every bus channel we subscribed to via the event map. */
	TArray<FDP_ListenerHandle> BusListenerHandles;

	/** FTSTicker handle for the per-frame crossfade/intensity drive. */
	FTSTicker::FDelegateHandle TickerHandle;

	// ---- MUSIC DEPTH (6) quantization state (all NEW) ----

	/** A pending quantized state change (invalid when none). Applied at the next matching boundary. */
	UPROPERTY(Transient)
	FGameplayTag PendingStateTag;

	/** Quantization boundary for the pending state change. */
	EAudio_MusicQuantize PendingStateQuantize = EAudio_MusicQuantize::Immediate;

	/** True when a pending intensity change is queued. */
	bool bHasPendingIntensity = false;

	/** Pending intensity target (applied at PendingIntensityQuantize boundary). */
	float PendingIntensityValue = 0.f;

	/** Quantization boundary for the pending intensity change. */
	EAudio_MusicQuantize PendingIntensityQuantize = EAudio_MusicQuantize::Immediate;

	/**
	 * Internal tempo PHASE in seconds since the active state began, advanced each tick by the sim clock
	 * (or real delta when no clock), used to detect beat/bar/phrase boundaries in the FTSTicker
	 * fallback. Reset on each state change so boundaries are measured from the state start.
	 */
	double TempoPhaseSeconds = 0.0;

	/** Phase value at the previous tick, so a boundary crossing can be detected this tick. */
	double LastTempoPhaseSeconds = 0.0;

	/** Optional caller-owned Quartz clock (weak: we never own it). When valid it drives boundaries. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UQuartzClockHandle> QuartzClock;

	/** Optional sim clock seam for pause/time-dilation-aware fallback phase. */
	TWeakInterfacePtr<ISeam_SimClock> SimClock;

	// ---- Internals ----

	/** Advance the tempo phase and apply any pending quantized transition whose boundary just passed. */
	void AdvanceQuantization(float DeltaTime);

	/** True if a beat/bar/phrase boundary was crossed between LastTempoPhaseSeconds and TempoPhaseSeconds. */
	bool DidCrossBoundary(EAudio_MusicQuantize Quantize) const;

	/** Apply (and clear) any pending state/intensity changes. */
	void FlushPendingState();
	void FlushPendingIntensity();

	/** Per-frame: advance all voice fades, ease intensity, retire finished voices. */
	bool Tick(float DeltaTime);

	/** (Re)subscribe to the bus for every distinct channel in the installed map. */
	void RefreshBusSubscriptions();

	/** Drop all current bus subscriptions. */
	void ClearBusSubscriptions();

	/** Handle a bus message by applying every matching rule in the installed map. */
	void HandleBusMessage(const FDP_Message& Message);

	/** Apply one resolved rule (SetState / TriggerStinger / SetIntensity). */
	void ApplyRule(const FAudio_MusicEventRule& Rule);

	/** Resolve a state tag to an asset: installed map playlist first, then core data registry. */
	UAudio_MusicStateDataAsset* ResolveState(FGameplayTag StateTag) const;

	/** Begin fading out every currently-active voice over Duration (used on state change/stop). */
	void RetireActiveVoices(float Duration);

	/** Create/restart looping voices for ActiveState's layers, fading them in over Duration. */
	void SpawnVoicesForActiveState(float Duration);

	/** Recompute each active-state voice's target volume from CurrentIntensity and apply (no fade). */
	void RemixActiveVoices();

	/** Get or create a pooled audio component bound to Sound, configured as a 2D looping music voice. */
	UAudioComponent* AcquireVoiceComponent(USoundBase* Sound);

	/** Stop and release a voice's component back for GC. */
	void ReleaseVoice(FAudio_MusicVoice& Voice);
};
