// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Reverb/Audio_ReverbMixProfileDataAsset.h"
#include "DesignPatternsAudioModule.h"
#include "Core/DPLog.h"

#include "Sound/SoundSubmix.h"
#include "Sound/SoundEffectSubmix.h"
#include "AudioMixerBlueprintLibrary.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

void UAudio_ReverbMixProfileDataAsset::ApplySubmixEffects(UObject* WorldContext, bool bActive, float BlendTimeOverride) const
{
	if (!WorldContext)
	{
		return; // No world context (and therefore no audio device) — safe no-op.
	}

	for (const FAudio_ReverbSubmixEffect& Entry : SubmixEffects)
	{
		if (Entry.TargetSubmix.IsNull() || Entry.EffectPreset.IsNull())
		{
			continue;
		}

		// A reverb-zone transition is deliberate and infrequent; resolving the soft refs synchronously
		// here is acceptable and the unloaded cost was zero until this overlap.
		USoundSubmix* Submix = Entry.TargetSubmix.LoadSynchronous();
		USoundEffectSubmixPreset* Preset = Entry.EffectPreset.LoadSynchronous();
		if (!Submix || !Preset)
		{
			UE_LOG(LogDP, Verbose,
				TEXT("ReverbProfile '%s': skipped a submix effect with an unloadable submix/preset."),
				*DataTag.ToString());
			continue;
		}

		if (bActive)
		{
			// WRAP the engine AudioMixer effect-chain API. Returns a chain handle id we do not need to
			// retain because we remove by preset on deactivation. No-op without an audio device.
			UAudioMixerBlueprintLibrary::AddSubmixEffect(WorldContext, Submix, Preset);
			UE_LOG(LogDP, Verbose, TEXT("ReverbProfile '%s': +effect '%s' on submix '%s' (wet %.2f)."),
				*DataTag.ToString(), *Preset->GetName(), *Submix->GetName(), Entry.WetLevel);
		}
		else
		{
			UAudioMixerBlueprintLibrary::RemoveSubmixEffectPreset(WorldContext, Submix, Preset);
			UE_LOG(LogDP, Verbose, TEXT("ReverbProfile '%s': -effect '%s' on submix '%s'."),
				*DataTag.ToString(), *Preset->GetName(), *Submix->GetName());
		}
	}

	// BlendTimeOverride is reserved for project preset tuning (the engine AddSubmixEffect API has no
	// blend parameter); honoured by projects that drive a wet-level fade on their preset. Logged so the
	// chosen transition time is observable.
	UE_LOG(LogDP, VeryVerbose, TEXT("ReverbProfile '%s': ApplySubmixEffects bActive=%d blend=%.2f."),
		*DataTag.ToString(), bActive ? 1 : 0, BlendTimeOverride);
}

FName UAudio_ReverbMixProfileDataAsset::GetDataAssetType_Implementation() const
{
	// Share the base mix-profile bucket so the registry/asset-manager scans reverb profiles together
	// with plain mix profiles and the sound manager can resolve them by tag the same way.
	return Super::GetDataAssetType_Implementation();
}

#if WITH_EDITOR
EDataValidationResult UAudio_ReverbMixProfileDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	for (int32 Index = 0; Index < SubmixEffects.Num(); ++Index)
	{
		const FAudio_ReverbSubmixEffect& Entry = SubmixEffects[Index];
		if (Entry.TargetSubmix.IsNull())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Reverb submix effect [%d] has no TargetSubmix."), Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (Entry.EffectPreset.IsNull())
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Reverb submix effect [%d] has no EffectPreset."), Index)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif
