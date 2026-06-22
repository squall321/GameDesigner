// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Music/Audio_MusicStateDataAsset.h"
#include "Core/DPLog.h"
#include "Sound/SoundBase.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

float FAudio_MusicLayer::ComputeTargetVolume(float Intensity) const
{
	// No stem -> contributes nothing. Avoids playing/allocating a silent voice.
	if (Stem.IsNull())
	{
		return 0.0f;
	}

	const float I = FMath::Clamp(Intensity, 0.0f, 1.0f);

	// --- Fade-in band: silence below FadeInStart, ramp to 1 by FadeInEnd. ---
	float InAlpha;
	if (I <= FadeInStart)
	{
		InAlpha = (FadeInStart <= 0.0f) ? 1.0f : 0.0f; // a layer that starts at 0 is audible from the floor
	}
	else if (I >= FadeInEnd)
	{
		InAlpha = 1.0f;
	}
	else
	{
		const float Span = FMath::Max(FadeInEnd - FadeInStart, KINDA_SMALL_NUMBER);
		InAlpha = (I - FadeInStart) / Span;
	}

	// --- Optional fade-out band: full below FadeOutStart, ramp to 0 by FadeOutEnd. ---
	float OutAlpha = 1.0f;
	if (FadeOutStart < 1.0f || FadeOutEnd < 1.0f)
	{
		if (I <= FadeOutStart)
		{
			OutAlpha = 1.0f;
		}
		else if (I >= FadeOutEnd)
		{
			OutAlpha = 0.0f;
		}
		else
		{
			const float Span = FMath::Max(FadeOutEnd - FadeOutStart, KINDA_SMALL_NUMBER);
			OutAlpha = 1.0f - ((I - FadeOutStart) / Span);
		}
	}

	const float Envelope = FMath::Clamp(InAlpha, 0.0f, 1.0f) * FMath::Clamp(OutAlpha, 0.0f, 1.0f);
	return FMath::Max(0.0f, Envelope * LayerVolume);
}

UAudio_MusicStateDataAsset::UAudio_MusicStateDataAsset()
{
	// States are addressed by tag at runtime; default display name is left empty for designers.
}

TSoftObjectPtr<USoundBase> UAudio_MusicStateDataAsset::FindStinger(FGameplayTag StingerTag) const
{
	if (const TSoftObjectPtr<USoundBase>* Found = Stingers.Find(StingerTag))
	{
		return *Found;
	}
	return TSoftObjectPtr<USoundBase>();
}

float UAudio_MusicStateDataAsset::GetSecondsPerBeat() const
{
	// No tempo => 0 (callers treat 0 as "no quantization possible").
	return (BeatsPerMinute > 0.f) ? (60.f / BeatsPerMinute) : 0.f;
}

float UAudio_MusicStateDataAsset::GetSecondsPerBar() const
{
	const float Beat = GetSecondsPerBeat();
	return (Beat > 0.f) ? (Beat * FMath::Max(1, BeatsPerBar)) : 0.f;
}

bool UAudio_MusicStateDataAsset::HasPlayableLayers() const
{
	for (const FAudio_MusicLayer& Layer : Layers)
	{
		if (!Layer.Stem.IsNull())
		{
			return true;
		}
	}
	return false;
}

FName UAudio_MusicStateDataAsset::GetDataAssetType_Implementation() const
{
	// Shared bucket for all music-state assets regardless of subclass.
	return FName(TEXT("Audio_MusicState"));
}

#if WITH_EDITOR
EDataValidationResult UAudio_MusicStateDataAsset::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	if (Layers.Num() == 0)
	{
		Context.AddWarning(FText::FromString(
			FString::Printf(TEXT("Music state '%s' has no layers; it will be silent."), *GetName())));
	}

	for (int32 Index = 0; Index < Layers.Num(); ++Index)
	{
		const FAudio_MusicLayer& Layer = Layers[Index];
		if (Layer.Stem.IsNull())
		{
			Context.AddWarning(FText::FromString(
				FString::Printf(TEXT("Music state '%s' layer %d has no stem assigned."), *GetName(), Index)));
		}
		if (Layer.FadeInEnd < Layer.FadeInStart)
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Music state '%s' layer %d: FadeInEnd < FadeInStart."), *GetName(), Index)));
			Result = EDataValidationResult::Invalid;
		}
		if (Layer.FadeOutEnd < Layer.FadeOutStart)
		{
			Context.AddError(FText::FromString(
				FString::Printf(TEXT("Music state '%s' layer %d: FadeOutEnd < FadeOutStart."), *GetName(), Index)));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}
#endif
