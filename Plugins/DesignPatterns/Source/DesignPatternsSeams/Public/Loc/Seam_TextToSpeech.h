// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_TextToSpeech.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_TextToSpeech : public UInterface
{
	GENERATED_BODY()
};

/**
 * Text-to-speech seam. The Localization module owns the registration slot; the host project supplies the
 * concrete backend (platform TTS / a middleware). Held weakly and inert (no-op) when unset, so the
 * framework never depends on a specific TTS implementation.
 */
class DESIGNPATTERNSSEAMS_API ISeam_TextToSpeech
{
	GENERATED_BODY()

public:
	/** Speak Text. Category lets the backend route/duck (e.g. UI vs narration). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Accessibility")
	void Speak(const FText& Text, FGameplayTag Category);

	/** Stop any in-progress speech. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Accessibility")
	void StopSpeaking();

	/** True if a real TTS backend is connected. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Accessibility")
	bool IsSpeechAvailable() const;
};
