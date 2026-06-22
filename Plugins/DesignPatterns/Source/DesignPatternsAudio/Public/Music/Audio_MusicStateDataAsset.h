// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Audio_MusicStateDataAsset.generated.h"

class USoundBase;

/**
 * One vertically-layered intensity stem within a music state.
 *
 * Adaptive ("vertical") music is built by stacking several looping stems that all play in sync
 * and are mixed in/out by an intensity scalar — e.g. a calm pad at low intensity, drums fading in
 * as intensity rises, a brass layer only at the top end. Each stem declares the intensity window
 * over which it is audible so the director can compute a per-stem target volume from a single
 * SetIntensity(float) call without any code change per state.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_MusicLayer
{
	GENERATED_BODY()

	/** The looping stem asset for this layer. Soft so unreferenced states don't bloat memory. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Layer")
	TSoftObjectPtr<USoundBase> Stem;

	/**
	 * Normalized intensity [0,1] at or above which this layer begins to fade IN. Below this the
	 * layer is silent. Combined with FadeInEnd to form the lower edge of the audible window.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Layer",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FadeInStart = 0.0f;

	/**
	 * Normalized intensity [0,1] at which this layer reaches full LayerVolume. Between FadeInStart
	 * and FadeInEnd the layer ramps linearly from silence to LayerVolume.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Layer",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FadeInEnd = 0.0f;

	/**
	 * Optional upper edge: normalized intensity above which the layer fades back OUT (e.g. a quiet
	 * pad that drops once combat peaks). Leave >= 1.0 to keep the layer present to the top. The
	 * fade-out spans [FadeOutStart, FadeOutEnd].
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Layer",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FadeOutStart = 1.0f;

	/** Normalized intensity at which the layer is fully silent again (top of the fade-out band). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Layer",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FadeOutEnd = 1.0f;

	/** Per-layer volume scalar applied on top of the computed intensity envelope (1.0 = unity). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Layer",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float LayerVolume = 1.0f;

	/**
	 * Compute this layer's target linear volume for a given normalized intensity, folding in
	 * both the fade-in band, the optional fade-out band, and LayerVolume. Result is clamped >= 0.
	 * Pure data math — no engine state — so it is unit-testable and called every blend tick.
	 */
	float ComputeTargetVolume(float Intensity) const;
};

/**
 * A single named music "state" keyed by GameplayTag (e.g. DP.Music.State.Explore,
 * DP.Music.State.Combat, DP.Music.State.Boss).
 *
 * IDENTITY: the inherited DataTag (a child of DP.Data.Music.State) is how the director and the
 * event-map reference this state — never by hard pointer or asset path. Resolved through the core
 * data registry (FindByTagTyped) or assigned directly on a director's playlist.
 *
 * CONTENT: a state owns its set of synchronized intensity LAYERS (vertical re-orchestration), the
 * crossfade duration used when transitioning INTO this state, and a map of STINGER one-shots
 * (short musical accents fired by TriggerStinger, e.g. an "enemy spotted" hit) keyed by a stinger
 * tag so the same state can host several context-specific accents.
 *
 * This is pure cosmetic/local content; nothing here is replicated. The director consumes it on
 * each client, driven by already-replicated gameplay surfaced on the message bus.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAUDIO_API UAudio_MusicStateDataAsset : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UAudio_MusicStateDataAsset();

	/**
	 * The synchronized looping stems that make up this state, mixed by intensity. Order is not
	 * significant; every layer starts playing together when the state becomes active so they stay
	 * phase-locked, and the director only adjusts their per-layer volumes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|State")
	TArray<FAudio_MusicLayer> Layers;

	/**
	 * Seconds to crossfade FROM the previously-active state INTO this one. The outgoing state fades
	 * out and this state's layers fade in over this duration. Designer-tunable per state so a tense
	 * combat cut can be near-instant while an explore return can be long and gentle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|State",
		meta = (ClampMin = "0.0", Units = "s"))
	float CrossfadeSeconds = 2.0f;

	/**
	 * Stinger one-shots available while this state is active, keyed by a stinger GameplayTag
	 * (a child of DP.Music.Stinger). TriggerStinger(Tag) plays the matching sound non-looping over
	 * the current bed without disturbing the running layers. Empty map = state has no stingers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|State")
	TMap<FGameplayTag, TSoftObjectPtr<USoundBase>> Stingers;

	/**
	 * Master volume scalar for the whole state (applied on top of each layer's computed volume).
	 * Lets designers balance one state louder/quieter than another without re-authoring layers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|State",
		meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float StateVolume = 1.0f;

	// ----------------------------------------------------------------------------------------------
	//  MUSIC DEPTH (6) ADDITIVE tempo metadata for bar-synced (quantized) transitions.
	//  Defaults reproduce the shipped free-running behaviour byte-for-byte (BeatsPerMinute = 0).
	// ----------------------------------------------------------------------------------------------

	/**
	 * Tempo in beats per minute used to derive beat/bar boundaries for quantized transitions in the
	 * FTSTicker fallback path. 0 (default) = NO tempo => every quantized transition behaves as
	 * Immediate, so existing states are unchanged. Ignored when a Quartz clock supplies the boundary.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Tempo", meta = (ClampMin = "0.0", UIMax = "240.0"))
	float BeatsPerMinute = 0.f;

	/** Beats per bar (time-signature numerator) for bar/phrase quantization. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Tempo", meta = (ClampMin = "1", UIMin = "1", UIMax = "16"))
	int32 BeatsPerBar = 4;

	/** Bars per phrase for NextPhrase quantization (e.g. 4 or 8 bar phrases). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Tempo", meta = (ClampMin = "1", UIMin = "1", UIMax = "64"))
	int32 PhraseBars = 4;

	/**
	 * Optional next state for HORIZONTAL re-sequencing: when set, the director may automatically
	 * advance to this state at a phrase boundary (e.g. an intro that hands off to a loop). Invalid =
	 * no automatic sequencing. Child of DP.Data.Music.State (the shipped state root).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Music|Tempo", meta = (Categories = "DP.Data.Music.State"))
	FGameplayTag SequenceNextStateTag;

	/** Seconds per beat from BeatsPerMinute, or 0 if no tempo. Pure helper for the quantize math. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music|Tempo")
	float GetSecondsPerBeat() const;

	/** Seconds per bar (GetSecondsPerBeat * BeatsPerBar), or 0 if no tempo. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music|Tempo")
	float GetSecondsPerBar() const;

	/** True when this state declares a usable tempo (BeatsPerMinute > 0). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music|Tempo")
	bool HasTempo() const { return BeatsPerMinute > 0.f; }

	/** Look up a stinger sound by tag; returns the soft pointer (may be unset) or an empty one. */
	UFUNCTION(BlueprintCallable, Category = "Music|State")
	TSoftObjectPtr<USoundBase> FindStinger(FGameplayTag StingerTag) const;

	/** True when this state declares at least one layer with an assigned stem. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Music|State")
	bool HasPlayableLayers() const;

	//~ Begin UDP_DataAsset
	/**
	 * Collapse every music state into one shared asset-manager bucket ("Audio_MusicState") so the
	 * director can scan/preload all states as a group regardless of concrete subclass.
	 */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Warns on layers with no stem and on inverted fade bands so content errors surface early. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
