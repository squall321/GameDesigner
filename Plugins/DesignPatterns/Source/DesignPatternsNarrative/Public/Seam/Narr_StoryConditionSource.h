// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "UObject/ScriptInterface.h"
#include "GameplayTagContainer.h"
#include "Narr_StoryConditionSource.generated.h"

UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UNarr_StoryConditionSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * The read/apply facade the shared narrative condition/effect mini-language calls back into.
 *
 * This is the SINGLE source-of-truth contract that BOTH the dialogue runner (gates a dialogue node /
 * choice) AND the story director (gates a story beat, applies its unlock effects) agree on. Conditions
 * (UNarr_Condition) read through it; effects (UNarr_Effect) write through it. Consumers depend only on
 * THIS interface, never on the concrete story-director or world-hub class, so the dialogue area evaluates
 * gating without referencing DesignPatternsWorld.
 *
 * These are RAW C++ virtuals (not BlueprintNativeEvent) and the interface is CannotImplementInterfaceIn-
 * Blueprint: it is called per condition leaf / per effect and must stay cheap and native. The shipped
 * implementation is UNarr_StoryDirectorSubsystem, which wraps the read-only IWorldHub_Queryable seam for
 * reads and the hub's authoritative write API for writes.
 *
 * AUTHORITY: all reads are const and side-effect free. Only ApplyFlag / ApplyCounter mutate, and the
 * implementation guards authority — they are safe no-ops on clients (the canonical state replicates back
 * through the world hub). Effects therefore never need to re-check authority themselves.
 */
class DESIGNPATTERNSNARRATIVE_API INarr_StoryConditionSource
{
	GENERATED_BODY()

public:
	/**
	 * @return true if the world-state boolean flag identified by Key is currently set (Global scope).
	 * A missing flag reads as bDefault. Pure read.
	 */
	virtual bool QueryFlag(const FGameplayTag& Key, bool bDefault = false) const = 0;

	/** @return the world-state counter for Key (Global scope), or Default when unset. Pure read. */
	virtual int64 QueryCounter(const FGameplayTag& Key, int64 Default = 0) const = 0;

	/**
	 * @return true if the narrative beat/arc identified by BeatOrArcTag is currently ACTIVE (in the
	 * director's tracked set). Lets a condition gate on "beat X has started but not finished". Pure read.
	 */
	virtual bool IsBeatActive(const FGameplayTag& BeatOrArcTag) const = 0;

	/**
	 * @return true if the narrative beat/arc identified by BeatOrArcTag has been COMPLETED at least once
	 * this session/save. Pure read.
	 */
	virtual bool IsBeatCompleted(const FGameplayTag& BeatOrArcTag) const = 0;

	/**
	 * Set a world-state boolean flag (Global scope). AUTHORITY ONLY — implementations early-return on
	 * clients. Used by unlock effects, never by conditions.
	 */
	virtual void ApplyFlag(const FGameplayTag& Key, bool bValue) = 0;

	/**
	 * Add Delta to a world-state counter (Global scope) and @return the new (clamped) value. AUTHORITY
	 * ONLY — returns the current value unchanged on clients.
	 */
	virtual int64 ApplyCounter(const FGameplayTag& Key, int64 Delta) = 0;
};
