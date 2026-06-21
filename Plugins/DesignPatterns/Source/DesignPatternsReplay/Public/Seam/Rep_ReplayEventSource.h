// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Timeline/Rep_ReplayEvent.h"
#include "Rep_ReplayEventSource.generated.h"

UINTERFACE(BlueprintType, MinimalAPI)
class URep_ReplayEventSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Seam over "a system that can contribute significant events to the replay timeline".
 *
 * The timeline (URep_ReplayTimeline) already harvests events automatically from the message bus
 * and the core command history. This seam lets ANY game system additionally push curated,
 * domain-specific markers (a boss phase change, a round end, an objective captured) WITHOUT the
 * timeline hard-depending on that system's concrete type.
 *
 * Resolution: implementers register themselves under Rep_NativeTags::Service_Replay in the
 * service locator (the locator aggregates multiple providers behind that tag is not supported —
 * instead implementers add themselves to the subsystem's source list via
 * URep_ReplaySubsystem::RegisterEventSource). The timeline polls each registered source once when
 * it begins recording (for already-known scripted markers) and the source may also call back into
 * the subsystem live via RecordEvent.
 *
 * All methods are pure reads on the recording machine; contributing events never mutates
 * replicated/authoritative state.
 */
class DESIGNPATTERNSREPLAY_API IRep_ReplayEventSource
{
	GENERATED_BODY()

public:
	/**
	 * Append any events this source already knows about at the given recording-relative time base
	 * to OutEvents. Called by the timeline when recording begins and on demand. Implementations
	 * MUST only add events; they must not clear or reorder OutEvents.
	 *
	 * @param RecordingTimeSeconds  The current recording-relative time (seconds) when polled.
	 * @param OutEvents             Accumulator the source appends its contributed events to.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Replay|EventSource")
	void GatherReplayEvents(float RecordingTimeSeconds, UPARAM(ref) TArray<FRep_ReplayEvent>& OutEvents) const;

	/** Stable identity of this source (for dedup/logging). Anchor under a game tag. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Replay|EventSource")
	FGameplayTag GetEventSourceId() const;
};
