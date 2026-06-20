// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Sequence/Narr_SequenceTypes.h"
#include "UObject/WeakInterfacePtr.h"
#include "Narr_SequenceDirectorComponent.generated.h"

class ULevelSequence;
class ULevelSequencePlayer;
class ALevelSequenceActor;
class ISeam_InputModeArbiter;

/** Fired locally when this director starts a cutscene. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNarr_OnSequenceStarted, FGameplayTag, SequenceTag);

/** Fired locally when this director's cutscene ends (with the reason). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FNarr_OnSequenceEnded, FGameplayTag, SequenceTag, ENarr_SequenceEndReason, Reason);

/**
 * A clean tag/data wrapper over the engine's ULevelSequencePlayer for cutscene playback.
 *
 * Attach to any actor (a director actor, a trigger volume, a level-script proxy). It WRAPS the engine
 * sequence player — it never reimplements playback — and layers the narrative concerns on top:
 *   - Play(ULevelSequence*) / Skip() / Stop() with FNarr_SequencePlayParams policy.
 *   - Input lock via the shared ISeam_InputModeArbiter seam (resolved from the service locator),
 *     pushed on start and popped on end. Never touches the player controller directly.
 *   - Observer-only DP.Bus.Narrative.Sequence.* events broadcast on start/finish/skip.
 *   - Cutscene playback is LOCAL/COSMETIC (not replicated). Its COMPLETION may set a world-hub flag
 *     (authority side) so persistent/replicated story state records the cutscene as seen.
 *
 * Net model: this component does NOT replicate. Each machine plays its own local cutscene driven by
 * already-replicated gameplay (a trigger overlap, a story beat, a direct call). The single
 * authoritative side-effect — the completion hub flag — is written through the world hub's
 * authority-guarded API, so clients calling it are no-ops.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (Collision, Physics, Lod, Activation, ComponentReplication))
class DESIGNPATTERNSNARRATIVE_API UNarr_SequenceDirectorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNarr_SequenceDirectorComponent();

	/**
	 * Play a level sequence with the supplied policy. Creates (or reuses) an engine
	 * ULevelSequencePlayer, applies the input lock if requested, broadcasts the Started event, and
	 * binds completion. Calling Play while already playing first stops the current playback (Aborted).
	 *
	 * @param Sequence    The level sequence asset to play (null is a logged no-op).
	 * @param SequenceTag Designer identity used in bus payloads / completion flags (may be empty).
	 * @param Params      Playback + narrative policy (input lock, skippable, completion flag).
	 * @return true if playback started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Sequence")
	bool Play(ULevelSequence* Sequence, FGameplayTag SequenceTag, const FNarr_SequencePlayParams& Params);

	/**
	 * Convenience: play DefaultSequence with DefaultSequenceTag and DefaultPlayParams (the data-driven
	 * trigger path uses this). @return true if playback started.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Sequence")
	bool PlayDefault();

	/**
	 * Skip the active cutscene if it is skippable. Stops playback early, ends with reason Skipped, and
	 * (per policy) may still set the completion flag. No-op if nothing is playing or not skippable.
	 * @return true if a cutscene was skipped.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Sequence")
	bool Skip();

	/** Stop the active cutscene immediately with reason Aborted (does not set the completion flag). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Sequence")
	void Stop();

	/** @return true while a cutscene is actively playing. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Sequence")
	bool IsPlaying() const { return bIsPlaying; }

	/** The tag of the currently-playing cutscene (empty when idle). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Sequence")
	FGameplayTag GetActiveSequenceTag() const { return ActiveSequenceTag; }

	/** Fired locally when a cutscene starts. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Narrative|Sequence")
	FNarr_OnSequenceStarted OnSequenceStarted;

	/** Fired locally when a cutscene ends. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Narrative|Sequence")
	FNarr_OnSequenceEnded OnSequenceEnded;

	// ---- Designer defaults (for PlayDefault / the trigger path) -------------------------------

	/** The cutscene asset played by PlayDefault / the trigger. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence")
	TObjectPtr<ULevelSequence> DefaultSequence;

	/** Identity tag used by PlayDefault for bus payloads / completion flags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence",
		meta = (Categories = "Narr.Sequence"))
	FGameplayTag DefaultSequenceTag;

	/** Playback + narrative policy used by PlayDefault. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence")
	FNarr_SequencePlayParams DefaultPlayParams;

protected:
	//~ Begin UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

private:
	/**
	 * Engine sequence player owned for the current playback. Created via
	 * ULevelSequencePlayer::CreateLevelSequencePlayer; kept alive as a UPROPERTY for the playback's
	 * lifetime and released on end.
	 */
	UPROPERTY(Transient)
	TObjectPtr<ULevelSequencePlayer> SequencePlayer;

	/** The spawned ALevelSequenceActor backing the player (engine-owned; tracked weakly to clean up). */
	UPROPERTY(Transient)
	TObjectPtr<ALevelSequenceActor> SequenceActor;

	/** Resolved input-mode arbiter (non-owning) used for the cutscene input lock. */
	TWeakInterfacePtr<ISeam_InputModeArbiter> InputArbiter;

	/** The request id returned by the arbiter's PushInputMode; popped on end. */
	FGuid InputLockRequest;

	/** Identity of the cutscene currently playing. */
	FGameplayTag ActiveSequenceTag;

	/** Policy captured for the current playback (so the completion flag / skip rules survive to end). */
	FNarr_SequencePlayParams ActiveParams;

	/** True between start and end. */
	bool bIsPlaying = false;

	/** Engine callback bound to the player's OnFinished delegate; ends with reason Finished. */
	UFUNCTION()
	void HandleSequenceFinished();

	/** Common teardown: release the input lock, broadcast end, set completion flag per reason, reset. */
	void EndPlayback(ENarr_SequenceEndReason Reason);

	/** Push the cutscene input mode onto the arbiter (resolved from the locator) if policy requires. */
	void AcquireInputLock();

	/** Release any held input lock. */
	void ReleaseInputLock();

	/** Set the completion hub flag (authority side) for the active playback, honoring the skip rule. */
	void TrySetCompletionFlag(ENarr_SequenceEndReason Reason);

	/** Broadcast a sequence bus event with a flat payload. */
	void BroadcastSequenceEvent(const FGameplayTag& Channel, ENarr_SequenceEndReason Reason) const;

	/** Tear down the engine player/actor after playback. */
	void DestroyPlayer();
};

// =============================================================================================
//  Trigger component
// =============================================================================================

/**
 * A data-driven trigger that plays its sibling/owner director's cutscene on overlap.
 *
 * Place on a trigger-volume actor that also carries (or owns) a UNarr_SequenceDirectorComponent.
 * On a qualifying overlap (by the local player pawn, by default) it calls PlayDefault on the resolved
 * director — once or every time per bFireOnce. The trigger holds no playback state itself; it is a
 * thin, designer-configured input into the director.
 *
 * Cosmetic/local: triggering is driven by the already-replicated pawn overlap on each machine, so the
 * cutscene plays locally without any trigger replication.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = (Collision, Physics, Lod, Activation, ComponentReplication))
class DESIGNPATTERNSNARRATIVE_API UNarr_SequenceTriggerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNarr_SequenceTriggerComponent();

	/**
	 * Explicit director to drive. When unset the trigger searches its owner for a
	 * UNarr_SequenceDirectorComponent at BeginPlay.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence")
	TObjectPtr<UNarr_SequenceDirectorComponent> Director;

	/** When true the trigger fires only once, then disarms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence")
	bool bFireOnce = true;

	/** When true only the LOCAL player pawn overlapping arms the trigger; other actors are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Narrative|Sequence")
	bool bRequireLocalPlayerPawn = true;

	/** Manually arm/disarm the trigger (e.g. re-arm a one-shot for a replay). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Narrative|Sequence")
	void SetArmed(bool bInArmed) { bArmed = bInArmed; }

	/** @return whether the trigger is currently armed. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Narrative|Sequence")
	bool IsArmed() const { return bArmed; }

protected:
	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

private:
	/** Whether the trigger will respond to overlaps. Cleared by a one-shot fire. */
	bool bArmed = true;

	/** Bound to the owner's primitive OnComponentBeginOverlap. */
	UFUNCTION()
	void HandleActorOverlap(class UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		class UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);

	/** Resolve and cache the director from the owner if not explicitly set. */
	UNarr_SequenceDirectorComponent* ResolveDirector();

	/** @return true if OtherActor qualifies to fire the trigger under the current policy. */
	bool DoesActorQualify(const AActor* OtherActor) const;
};
