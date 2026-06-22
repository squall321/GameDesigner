// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4 and CoreUObject on 5.5+. The optional caption
// payload a VO requester attaches (an FLoc_SubtitleLine the producer built) is carried opaquely.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Audio_VOTypes.generated.h"

/**
 * How an incoming VO line interacts with a currently-playing line of equal-or-different priority.
 */
UENUM(BlueprintType)
enum class EAudio_VOInterrupt : uint8
{
	/** Queue behind the active line; play when it finishes (default barks/dialogue ordering). */
	Queue,

	/** Stop the active line immediately and play this one (urgent line, e.g. "behind you!"). */
	Interrupt,

	/** Drop this line if anything of >= priority is already playing (low-value chatter). */
	DropIfBusy
};

/**
 * A request to play a VO/bark line. Built by the producer (gameplay/AI/narrative) and passed to the
 * VO subsystem. The audio module is CAPTION-AGNOSTIC: it never authors text. To surface a subtitle the
 * producer attaches an opaque CaptionPayload (an FLoc_SubtitleLine it built) and the subsystem
 * re-broadcasts it verbatim on DP.Bus.Loc.VoiceLine so the shipped ULoc_SubtitleSubsystem shows it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_VORequest
{
	GENERATED_BODY()

	/** VO line identity (child of DP.Audio.VO), resolved from a VO bank. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VO", meta = (Categories = "DP.Audio.VO"))
	FGameplayTag LineTag;

	/** When true play spatialized at WorldLocation; otherwise play 2D (UI/narrator). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VO")
	bool bAtLocation = false;

	/** World location for spatialized playback (used only when bAtLocation is true). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VO")
	FVector WorldLocation = FVector::ZeroVector;

	/**
	 * Priority override for queue arbitration (>= 0 replaces the bank entry's default priority; < 0
	 * uses the entry default). Higher wins; an interrupt only succeeds against a lower-or-equal line.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VO", meta = (ClampMin = "-1", UIMin = "-1", UIMax = "100"))
	int32 PriorityOverride = -1;

	/** Interaction with the currently-playing line. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VO")
	EAudio_VOInterrupt InterruptMode = EAudio_VOInterrupt::Queue;

	/** Per-call linear volume multiplier (1.0 = bank default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VO", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float VolumeMult = 1.f;

	/**
	 * OPTIONAL opaque caption payload the producer built (an FLoc_SubtitleLine). When set, the VO
	 * subsystem re-broadcasts it on DP.Bus.Loc.VoiceLine when the line STARTS playing, so subtitles
	 * appear — without the audio module ever depending on the Localization module or owning any text.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "VO")
	FInstancedStruct CaptionPayload;
};

/**
 * One VO line in a VO bank: the data fully describing how to play a single tag-keyed VO/bark.
 *
 * Mirrors the shape of FAudio_SoundEntry. There is deliberately NO subtitle FText here — captions are
 * the producer's / Narrative's responsibility (attached on the request and forwarded on the bus),
 * keeping audio caption-agnostic.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_VOEntry
{
	GENERATED_BODY()

	/** The VO sound to play. Soft so the bank stays unloaded until a line is requested. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO")
	TSoftObjectPtr<class USoundBase> Sound;

	/** Optional spatialization for at-location playback. Null = engine/asset default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO")
	TSoftObjectPtr<class USoundAttenuation> Attenuation;

	/**
	 * Category this VO belongs to (child of DP.Audio.Category, normally a Voice child). Drives category
	 * volume and lets the line be stopped via StopCategory.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO", meta = (Categories = "DP.Audio.Category"))
	FGameplayTag Category;

	/** Default queue priority for this line (higher wins). Overridable per request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO", meta = (ClampMin = "0", UIMax = "100"))
	int32 DefaultPriority = 10;

	/** Default linear volume for this line (multiplied by the per-call VolumeMult and category volume). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO", meta = (ClampMin = "0.0", UIMax = "4.0"))
	float DefaultVolume = 1.f;

	/** True if this line is a BARK (short, throttled by cooldown via TryBark). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO")
	bool bIsBark = false;

	/** Minimum seconds between successive plays of THIS bark line (TryBark refuses inside the window). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO", meta = (ClampMin = "0.0", UIMax = "60.0", Units = "s", EditCondition = "bIsBark"))
	float BarkCooldownSeconds = 4.f;

	/** Optional speaker identity tag (informational; e.g. for project subtitle styling). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO")
	FGameplayTag SpeakerTag;

	/**
	 * Optional duck-bus tag (child of DP.Audio.Mix.Duck) pushed for the duration of this line so it
	 * ducks music/SFX. Invalid = no automatic ducking for this line.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "VO", meta = (Categories = "DP.Audio.Mix.Duck"))
	FGameplayTag DuckBusTag;
};

/**
 * A live / queued VO entry inside the subsystem. Internal bookkeeping (not Blueprint-exposed for edit).
 */
USTRUCT()
struct FAudio_VOQueueEntry
{
	GENERATED_BODY()

	/** Stable handle returned to the requester (for StopVO). */
	UPROPERTY()
	FGuid Handle;

	/** The original request (carries the optional caption payload + interrupt mode). */
	UPROPERTY()
	FAudio_VORequest Request;

	/** Effective resolved priority (request override or entry default). */
	UPROPERTY()
	int32 Priority = 0;

	/** Resolved category for this line (from the bank entry). */
	UPROPERTY()
	FGameplayTag Category;

	/** Resolved duck-bus tag for this line (from the bank entry). */
	UPROPERTY()
	FGameplayTag DuckBusTag;

	/** Monotonic enqueue order, used to break priority ties (lower = earlier = plays first). */
	UPROPERTY()
	int64 Sequence = 0;
};
