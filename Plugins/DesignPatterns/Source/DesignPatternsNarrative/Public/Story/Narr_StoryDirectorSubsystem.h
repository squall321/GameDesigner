// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Seam/Narr_StoryConditionSource.h"   // INarr_StoryConditionSource (implemented here)
#include "Persist/Seam_Persistable.h"
#include "UObject/WeakInterfacePtr.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Narr_StoryDirectorSubsystem.generated.h"

class UNarr_StoryBeatDataAsset;
class UNarr_Condition;
class IWorldHub_Queryable;
class ISeam_SimClock;

/**
 * Flat, weak-ref-free bus payload for a story beat/arc lifecycle event.
 *
 * Carried inside an FInstancedStruct on the message bus. Holds only tags (no UObject/weak refs) so it
 * is safe to queue for deferred dispatch and to flatten across machines.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_StoryEventPayload
{
	GENERATED_BODY()

	/** The beat involved (or, for arc-level events, the arc tag in this slot). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|Story")
	FGameplayTag BeatTag;

	/** The arc the beat belongs to (empty for arc-only events that put the arc in BeatTag). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Narrative|Story")
	FGameplayTag ArcTag;

	FNarr_StoryEventPayload() = default;
	FNarr_StoryEventPayload(const FGameplayTag& InBeat, const FGameplayTag& InArc)
		: BeatTag(InBeat), ArcTag(InArc) {}
};

/**
 * The durable save record the story director captures/restores via ISeam_Persistable.
 *
 * Holds only the director's local TRACKING state (which beats are active / completed). The canonical
 * story flags themselves live in (and are replicated/saved by) the World hub; this record only
 * re-seeds the in-memory active/completed sets so the director resumes its graph traversal after a
 * load without re-running effects.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSNARRATIVE_API FNarr_StorySaveRecord
{
	GENERATED_BODY()

	/** Currently-active beats (started, not yet completed). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Story")
	TArray<FGameplayTag> ActiveBeats;

	/** Beats completed at least once this save. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Story")
	TArray<FGameplayTag> CompletedBeats;

	/** Arcs that have started (their first beat activated). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Story")
	TArray<FGameplayTag> StartedArcs;

	/** Arcs that have completed (all tracked beats done). */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "DesignPatterns|Narrative|Story")
	TArray<FGameplayTag> CompletedArcs;
};

/** Fired (locally) when the director's beat/arc tracking changes, for UI/quest-log binding. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNarr_OnStoryProgress, FGameplayTag, BeatTag, FGameplayTag, ArcTag);

/**
 * The authoritative branching STORY DIRECTOR.
 *
 * GameInstance-scoped so story progression survives level travel. Tracks which beats/arcs are active
 * and completed (a lightweight in-memory graph cursor), gates beat activation on world-hub conditions
 * (the shared UNarr_Condition mini-language, evaluated through INarr_StoryConditionSource), applies a
 * beat's unlock effects on completion, advances along the authored NextBeats branch graph, and
 * broadcasts observer-only DP.Bus.Narrative.Story.* events.
 *
 * Responsibilities and boundaries:
 *   - IMPLEMENTS INarr_StoryConditionSource: it is a shipped condition-evaluation context. Reads wrap
 *     the read-only world-hub query seam (IWorldHub_Queryable, resolved from the locator) and an
 *     optional ISeam_SimClock; every read FAILS CLOSED when its backend is unavailable.
 *   - IMPLEMENTS ISeam_Persistable: save/restore the director's TRACKING sets only. The canonical
 *     story FLAGS live in the World hub (which replicates AND saves them) — this director does NOT
 *     duplicate replicated story state; it mirrors completion into hub flags so other systems and the
 *     dialogue runner read it through the standard query seam.
 *   - AUTHORITY: every mutator (RequestBeat / CompleteBeat / advance) guards authority at the TOP and
 *     no-ops on clients. Clients observe progression through the replicated hub flags + bus events
 *     re-broadcast from already-replicated state.
 *   - HasClock(): reports whether an ISeam_SimClock is available (resolved from the locator), so
 *     time-gated content fails closed rather than guessing a time of day.
 *
 * The director self-registers under DP.Service.Narrative.ConditionSource and
 * DP.Service.Narrative.StoryDirector (WeakObserved) so gameplay/UI resolve it by tag.
 */
UCLASS()
class DESIGNPATTERNSNARRATIVE_API UNarr_StoryDirectorSubsystem
	: public UDP_GameInstanceSubsystem
	, public INarr_StoryConditionSource
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * GameInstance subsystems have no HasWorldAuthority(); derive it from the current world's net mode.
	 * True on server / standalone / listen-server host. Every story mutator gates on this.
	 */
	bool HasWorldAuthority() const;

	// ---- Story progression API (AUTHORITY ONLY for mutation) ----------------------------------

	/**
	 * Request that the beat identified by BeatTag becomes active. AUTHORITY ONLY.
	 *
	 * Resolves the beat asset from the data registry, evaluates its prerequisites against this
	 * director (as the condition source), and on success adds it to the active set, marks its arc
	 * started, broadcasts BeatStarted (and ArcStarted if first in the arc), and — if the beat is set to
	 * auto-complete — immediately completes it. On prerequisite failure it broadcasts BeatBlocked.
	 *
	 * @return true if the beat was activated (or already active), false if blocked/unknown/non-authority.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Story")
	bool RequestBeat(FGameplayTag BeatTag);

	/**
	 * Complete an active beat. AUTHORITY ONLY.
	 *
	 * Applies the beat's completion effects (each self-guards authority), sets the canonical completion
	 * hub flag, moves the beat from active to completed, broadcasts BeatCompleted, advances to eligible
	 * NextBeats, and raises ArcCompleted when the arc's tracked beats are all done.
	 *
	 * @return true if a previously-active beat was completed, false if it was not active / non-authority.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Story")
	bool CompleteBeat(FGameplayTag BeatTag);

	/**
	 * @return true if BeatTag is currently active (started, not completed).
	 * BP-named to avoid C++ overload ambiguity with the INarr_StoryConditionSource override below
	 * (which differs only by taking a const-ref); both resolve to the same active-set lookup.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Story", meta = (DisplayName = "Is Beat Active"))
	bool BP_IsBeatActive(FGameplayTag BeatTag) const { return ActiveBeats.Contains(BeatTag); }

	/** @return true if BeatTag has been completed at least once this save. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Story", meta = (DisplayName = "Is Beat Completed"))
	bool BP_IsBeatCompleted(FGameplayTag BeatTag) const { return CompletedBeats.Contains(BeatTag); }

	/** @return true if ArcTag has started (its first beat activated). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Story")
	bool IsArcStarted(FGameplayTag ArcTag) const { return StartedArcs.Contains(ArcTag); }

	/** @return true if ArcTag has completed (all its tracked beats done). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Story")
	bool IsArcCompleted(FGameplayTag ArcTag) const { return CompletedArcs.Contains(ArcTag); }

	/** Snapshot the currently-active beats (copy). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Story")
	TArray<FGameplayTag> GetActiveBeats() const { return ActiveBeats.Array(); }

	/** Fired (locally, server + clients) whenever a beat starts. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Narrative|Story")
	FNarr_OnStoryProgress OnBeatStarted;

	/** Fired (locally, server + clients) whenever a beat completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Narrative|Story")
	FNarr_OnStoryProgress OnBeatCompleted;

	// ---- INarr_StoryConditionSource (raw C++ virtuals, fail-closed) ---------------------------
	virtual bool QueryFlag(const FGameplayTag& Key, bool bDefault = false) const override;
	virtual int64 QueryCounter(const FGameplayTag& Key, int64 Default = 0) const override;
	virtual bool IsBeatActive(const FGameplayTag& BeatOrArcTag) const override;
	virtual bool IsBeatCompleted(const FGameplayTag& BeatOrArcTag) const override;
	virtual void ApplyFlag(const FGameplayTag& Key, bool bValue) override;
	virtual int64 ApplyCounter(const FGameplayTag& Key, int64 Delta) override;

	/** @return whether a simulation-clock seam is currently available (for time-gated content). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Story")
	bool HasClock() const;

	// ---- ISeam_Persistable -------------------------------------------------------------------
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Currently-active beats (started, not completed). */
	TSet<FGameplayTag> ActiveBeats;

	/** Beats completed at least once this save. */
	TSet<FGameplayTag> CompletedBeats;

	/** Arcs whose first beat has activated. */
	TSet<FGameplayTag> StartedArcs;

	/** Arcs all of whose tracked beats are done. */
	TSet<FGameplayTag> CompletedArcs;

	/** Cached read seam onto the world hub (resolved lazily via the service locator). Non-owning. */
	mutable TWeakInterfacePtr<IWorldHub_Queryable> CachedHubQuery;

	/** Resolve (and cache) the world-hub read seam. @return the interface or null. */
	IWorldHub_Queryable* GetHubQuery() const;

	/** Resolve the concrete world hub subsystem for AUTHORITATIVE writes / typed reads (null on failure). */
	class UWorldHub_StateHubSubsystem* GetHubAuthority() const;

	/** Resolve a beat asset by its tag from the data registry (synchronous load). Null if unknown. */
	UNarr_StoryBeatDataAsset* ResolveBeat(const FGameplayTag& BeatTag) const;

	/** The canonical world-hub flag key marking a beat completed (override or derived from the tag). */
	FGameplayTag GetBeatCompletionFlagKey(const UNarr_StoryBeatDataAsset* Beat, const FGameplayTag& BeatTag) const;

	/** Broadcast a story event on the bus with a flat payload. */
	void BroadcastStoryEvent(const FGameplayTag& Channel, const FGameplayTag& BeatTag, const FGameplayTag& ArcTag) const;

	/** Advance from a just-completed beat to its eligible NextBeats (exclusive or all-eligible). */
	void AdvanceFromBeat(const UNarr_StoryBeatDataAsset* CompletedBeatAsset);

	/** Re-evaluate whether ArcTag is now complete (no active beats remain in it) and emit if so. */
	void TryCompleteArc(const FGameplayTag& ArcTag);

	/** Self-register under the Narrative condition-source and director service keys (WeakObserved). */
	void RegisterServices();

	/** Build a TScriptInterface<INarr_StoryConditionSource> pointing at this director. */
	TScriptInterface<INarr_StoryConditionSource> AsConditionSource() const;
};
