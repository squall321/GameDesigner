// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Loc/Seam_TextToSpeech.h"

// INERT native defaults for the text-to-speech seam. A project with no TTS backend leaves these
// unoverridden: Speak() is a no-op (nothing is spoken), StopSpeaking() is a no-op, and
// IsSpeechAvailable() returns false — so code that checks availability before speaking degrades
// gracefully and the framework is never forced to depend on a specific TTS implementation.
// The real project-side adapter (e.g. a platform TTS bridge) overrides all three methods.

void ISeam_TextToSpeech::Speak_Implementation(const FText& /*Text*/, FGameplayTag /*Category*/)
{
	// No TTS backend connected — inert no-op.
}

void ISeam_TextToSpeech::StopSpeaking_Implementation()
{
	// No TTS backend connected — inert no-op.
}

bool ISeam_TextToSpeech::IsSpeechAvailable_Implementation() const
{
	// Without a concrete backend, speech is never available.
	return false;
}
