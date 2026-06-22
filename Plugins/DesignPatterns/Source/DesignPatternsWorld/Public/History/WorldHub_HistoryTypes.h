// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"
#include "Save/WorldHub_Snapshot.h"
#include "WorldHub_HistoryTypes.generated.h"

/**
 * One recorded frame of world-hub state in the history ring buffer.
 *
 * A frame pairs a captured FWorldHub_Snapshot (which carries the FInstancedStruct payloads — LOCAL /
 * SAVE only, NEVER replicated) with deterministic, pause-aware timing metadata sourced from the
 * shared ISeam_SimClock (so two recordings of the same simulation produce identical timestamps). The
 * optional LabelTag turns a frame into a named checkpoint that RewindToLabel / RewindToCheckpoint can
 * target.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_HistoryFrame
{
	GENERATED_BODY()

	/** Monotonic index assigned on capture (never reused even after ring-buffer eviction). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|History")
	int32 FrameIndex = 0;

	/** Accumulated simulation seconds at capture (DeltaTime * clock time-scale; 0 when no clock). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|History")
	double SimTimeSeconds = 0.0;

	/** Calendar day index at capture (ISeam_SimClock::GetDayNumber), for day-granular tooling. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|History")
	int32 DayNumber = 0;

	/** Optional checkpoint label; an invalid tag denotes an ordinary (cadence/dirty) frame. */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|History")
	FGameplayTag LabelTag;

	/** The captured world-hub snapshot (FInstancedStruct payloads are LOCAL/SAVE only). */
	UPROPERTY(BlueprintReadOnly, SaveGame, Category = "DesignPatterns|WorldHub|History")
	FWorldHub_Snapshot Snapshot;

	FWorldHub_HistoryFrame() = default;

	/** @return true if this frame carries a named checkpoint label. */
	bool IsCheckpoint() const { return LabelTag.IsValid(); }
};

/** The direction in which a single (Scope, Key) slot differs between two history frames. */
UENUM(BlueprintType)
enum class EWorldHub_DeltaKind : uint8
{
	/** Present in the TO frame but absent in the FROM frame. */
	Added,

	/** Present in both frames but with a different stored value. */
	Changed,

	/** Present in the FROM frame but absent in the TO frame. */
	Removed
};

/**
 * One diff entry between two history frames. Pure read data (no UObject refs), safe to build and
 * inspect on clients. The previous/next values are the full snapshot value records so callers can
 * recover both the old and new payloads.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_StateDelta
{
	GENERATED_BODY()

	/** The scope of the slot that differs. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|History")
	FWorldHub_Scope Scope;

	/** The key of the slot that differs. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|History")
	FGameplayTag Key;

	/** How the slot differs (added / changed / removed). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|History")
	EWorldHub_DeltaKind Kind = EWorldHub_DeltaKind::Changed;

	/** The value in the FROM frame (unset/empty when Kind == Added). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|History")
	FWorldHub_FlagValue PreviousValue;

	/** The value in the TO frame (unset/empty when Kind == Removed). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|History")
	FWorldHub_FlagValue NextValue;

	FWorldHub_StateDelta() = default;
};

/**
 * Flat, weak-ref-free payload broadcast on the bus when a frame is captured or a rewind occurs.
 * Carried inside an FInstancedStruct by the bus; holds no UObject/weak references so it is safe to
 * queue for deferred dispatch.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLD_API FWorldHub_HistoryBusPayload
{
	GENERATED_BODY()

	/** The frame index involved (the captured frame, or the frame rewound to). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|History")
	int32 FrameIndex = 0;

	/** The checkpoint label involved (invalid for ordinary cadence frames). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|History")
	FGameplayTag LabelTag;

	/** Simulation seconds at the event. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|WorldHub|History")
	double SimTimeSeconds = 0.0;

	FWorldHub_HistoryBusPayload() = default;
	FWorldHub_HistoryBusPayload(int32 InFrameIndex, const FGameplayTag& InLabel, double InSimTime)
		: FrameIndex(InFrameIndex), LabelTag(InLabel), SimTimeSeconds(InSimTime) {}
};
