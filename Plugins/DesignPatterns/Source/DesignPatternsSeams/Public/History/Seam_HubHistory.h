// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Seam_HubHistory.generated.h"

UINTERFACE(MinimalAPI)
class USeam_HubHistory : public UInterface
{
	GENERATED_BODY()
};

/**
 * Rewind-to-checkpoint + event-sequence read/replay seam, owned by the World hub's history /
 * event-log subsystems. Replay, quest and debug tooling trigger a rewind and read the mutation
 * event stream through this seam WITHOUT depending on the DesignPatternsWorld module.
 *
 * Raw C++ virtuals to match the ISeam_Reputation / ISeam_FactionStanding house style (a lightweight
 * per-leaf read seam, not a Blueprint bridge). Events are flattened to LOCAL FInstancedStruct values
 * at the seam boundary so consumers never see the World module's concrete event/snapshot types.
 *
 * RewindToCheckpoint is authority-gated INSIDE the implementation (a client call is a safe no-op that
 * returns false) — the seam itself makes no authority promise to the caller, matching the rule that
 * every mutator of authoritative state guards HasAuthority() at the top of the impl.
 */
class DESIGNPATTERNSSEAMS_API ISeam_HubHistory
{
	GENERATED_BODY()

public:
	/**
	 * Restore world-hub state to the snapshot captured under CheckpointLabel.
	 * AUTHORITY-gated in the implementation: returns false (no-op) on clients or when no frame
	 * carries the label. @return true if a matching checkpoint was found and applied.
	 */
	virtual bool RewindToCheckpoint(FGameplayTag CheckpointLabel) = 0;

	/** The sequence number of the most-recently-appended mutation event (0 when the log is empty). */
	virtual int64 GetLatestEventSequence() const = 0;

	/**
	 * Append every mutation event with a sequence strictly greater than Sequence into OutFlattened,
	 * each flattened to a LOCAL FInstancedStruct (the seam's PII-/coupling-safe boundary form).
	 * OutFlattened is reset first. Safe on clients (pure read).
	 */
	virtual void GetEventsSince(int64 Sequence, TArray<FInstancedStruct>& OutFlattened) const = 0;
};
