// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Audio_MusicQuantize.generated.h"

/**
 * MUSIC DEPTH (6). When a quantized music transition should actually take effect.
 *
 * A quantized SetMusicState/SetIntensity is deferred until the chosen musical boundary so a horizontal
 * re-sequence lands on the beat rather than cutting mid-phrase. With a Quartz clock the boundary is
 * exact; in the FTSTicker fallback the director derives the boundary from the active state's
 * BeatsPerMinute / BeatsPerBar and an internal phase accumulator (advanced by the sim clock when one
 * is present, so it respects pause / time-dilation).
 */
UENUM(BlueprintType)
enum class EAudio_MusicQuantize : uint8
{
	/** Apply immediately (no quantization) — identical to the non-quantized API. */
	Immediate,

	/** Defer to the next beat. */
	NextBeat,

	/** Defer to the next bar (downbeat). The common musical transition boundary. */
	NextBar,

	/** Defer to a multi-bar phrase boundary (every PhraseBars bars from the state start). */
	NextPhrase
};
