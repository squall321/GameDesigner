// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Mix/Audio_MixProfileDataAsset.h"
#include "Audio_ReverbMixProfileDataAsset.generated.h"

class USoundSubmix;
class USoundEffectSubmixPreset;
class UObject;

/**
 * One submix-effect entry applied by a reverb-zone profile while it is the active top-of-stack.
 *
 * REVERB ZONES (2). Carries the data needed to push a single reverb (or any submix) effect preset
 * onto a target submix's effect chain over the engine AudioMixer. Both the target submix and the
 * effect preset are SOFT references so a reverb-zone bank costs nothing in memory until a listener
 * actually overlaps the zone and the profile is pushed.
 *
 * Wet level / fade are designer tunables (no magic numbers); they are honoured through the AudioMixer
 * dry/wet stage where the engine exposes it and are otherwise applied as a submix output trim.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_ReverbSubmixEffect
{
	GENERATED_BODY()

	/** Submix whose effect chain receives the preset (soft; loaded only when the profile is applied). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reverb")
	TSoftObjectPtr<USoundSubmix> TargetSubmix;

	/**
	 * The submix-effect preset to add to the target submix's chain (e.g. a SubmixEffectReverb preset).
	 * Soft so the (potentially heavy) preset only loads when a listener enters the zone.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reverb")
	TSoftObjectPtr<USoundEffectSubmixPreset> EffectPreset;

	/**
	 * Target wet level [0,1] for this effect while the profile is active. Used as the submix dry/wet
	 * mix where the engine exposes a per-effect wet stage; otherwise documented as the effect's intended
	 * intensity for the project's preset tuning. 1.0 = fully wet.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reverb", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WetLevel = 1.f;

	/** Seconds to blend the effect in on apply / out on remove (0 = instant). Designer-tunable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reverb", meta = (ClampMin = "0.0", UIMax = "5.0", Units = "s"))
	float FadeTime = 0.75f;
};

/**
 * REVERB ZONES (2). Additive subclass of the shipped UAudio_MixProfileDataAsset that, on top of the
 * base profile's submix volume overrides and duck rules, declares a set of SUBMIX EFFECT entries
 * (reverb presets) to add to submix effect chains while this profile is the active top-of-stack.
 *
 * It is pushed/popped through the EXISTING priority-stack machinery of UAudio_MixController, so it
 * blends and orders exactly like every other mix profile. The effect-chain add/remove is driven from
 * the new UAudio_MixController::ApplyExtraSubmixEffects hook, which calls ApplySubmixEffects on
 * activation/deactivation. The wrapping is over the engine AudioMixer
 * (UAudioMixerBlueprintLibrary::AddSubmixEffect / RemoveSubmixEffect) — never a hand-rolled reverb.
 *
 * Base mix profiles are completely unaffected: a plain UAudio_MixProfileDataAsset is never reverb,
 * so the controller's hook leaves it alone.
 *
 * Purely cosmetic/local; pushed per-client by AAudio_ReverbZoneVolume on the local listener overlap.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSAUDIO_API UAudio_ReverbMixProfileDataAsset : public UAudio_MixProfileDataAsset
{
	GENERATED_BODY()

public:
	/** Submix effects layered on while this reverb profile is the active mix snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Reverb")
	TArray<FAudio_ReverbSubmixEffect> SubmixEffects;

	/**
	 * Add (bActive=true) or remove (bActive=false) this profile's submix effects over the AudioMixer.
	 *
	 * @param WorldContext       A live world-context object (the mix controller). No-op without a world
	 *                           / audio device, so it is headless-safe.
	 * @param bActive            True to add the effects, false to remove them.
	 * @param BlendTimeOverride  If >= 0, overrides every entry's FadeTime for this transition (the
	 *                           dynamic-mixing blended push uses this); < 0 uses each entry's FadeTime.
	 *
	 * Soft refs are synchronously resolved here because a reverb zone overlap is a deliberate, infrequent
	 * transition and the unloaded cost was zero until this moment.
	 */
	void ApplySubmixEffects(UObject* WorldContext, bool bActive, float BlendTimeOverride) const;

	//~ Begin UDP_DataAsset
	/** Collapse reverb profiles into the SAME bucket as base mix profiles ("Audio_MixProfile"). */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Flags effect entries with no target submix or no preset, in addition to base validation. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
