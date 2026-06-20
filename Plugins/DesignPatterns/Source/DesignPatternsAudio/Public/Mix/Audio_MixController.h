// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Audio_MixController.generated.h"

class UAudio_MixProfileDataAsset;
class USoundSubmix;

/**
 * Resolved, applied snapshot of an active mix profile pushed onto the controller stack.
 *
 * Holds a strong (UPROPERTY) reference to the resolved profile so it cannot be GC'd while active,
 * the request handle the pusher uses to pop exactly its own push, and the effective priority used
 * for stack ordering.
 */
USTRUCT()
struct DESIGNPATTERNSAUDIO_API FAudio_ActiveMixSnapshot
{
	GENERATED_BODY()

	/** Opaque handle returned to the pusher; pop by handle so unrelated pushes are undisturbed. */
	UPROPERTY()
	FGuid Handle;

	/** The resolved, loaded mix profile. Strong so it stays alive while on the stack. */
	UPROPERTY()
	TObjectPtr<UAudio_MixProfileDataAsset> Profile = nullptr;

	/** Effective stack priority (profile Priority, optionally overridden by the push call). */
	UPROPERTY()
	int32 Priority = 0;

	/** Monotonic push sequence, used to break priority ties (higher = pushed later = wins). */
	UPROPERTY()
	int64 Sequence = 0;
};

/**
 * Applies mix profiles (submix volume snapshots + duck rules) over the engine's AudioMixer.
 *
 * Owned/instanced by the sound manager subsystem. Maintains a PRIORITY STACK of pushed profiles;
 * the highest-priority profile (ties: last pushed) is the "active" snapshot whose submix overrides
 * are applied via UGameplayStatics::SetSubmixOutputVolume. When the active profile changes (push or
 * pop), previously-overridden submixes that the new active profile does not touch are restored to
 * unity, and the new active profile's overrides are applied with their fade times.
 *
 * Duck rules from the active profile are surfaced via GetActiveDuckVolume so the sound manager can
 * scale category voice volumes. This is purely cosmetic/local state and is never replicated.
 *
 * Headless / no-audio-device safe: SetSubmixOutputVolume is a no-op without a device, and all stack
 * bookkeeping still works so push/pop semantics are consistent on a dedicated server.
 */
UCLASS()
class DESIGNPATTERNSAUDIO_API UAudio_MixController : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Push a resolved mix profile onto the stack. If PriorityOverride >= 0 it replaces the profile's
	 * own Priority for stack ordering. Recomputes and applies the active snapshot.
	 * @return a handle to pass to Pop; invalid if Profile is null.
	 */
	FGuid PushProfile(UAudio_MixProfileDataAsset* Profile, int32 PriorityOverride = -1);

	/** Pop a previously-pushed profile by handle (no-op if unknown). Recomputes the active snapshot. */
	void PopProfile(const FGuid& Handle);

	/** Pop every active profile and restore all touched submixes to unity. */
	void ClearAll();

	/** The DataTag of the currently-active (top-of-stack) profile, or an empty tag if none. */
	FGameplayTag GetActiveProfileTag() const;

	/**
	 * Effective duck multiplier for Category from the active profile's duck rules (deepest matching
	 * rule wins, walking the tag hierarchy). Returns 1.0 (no duck) if no rule applies. A negative
	 * DuckVolume in a rule resolves to the project default duck volume from settings.
	 */
	float GetActiveDuckVolume(const FGameplayTag& Category) const;

	/** Number of profiles currently on the stack. */
	int32 GetStackDepth() const { return Stack.Num(); }

	/** One-line status for the sound manager's debug string. */
	FString GetDebugString() const;

private:
	/** Active pushes, unordered; the active snapshot is computed by priority+sequence each change. */
	UPROPERTY()
	TArray<FAudio_ActiveMixSnapshot> Stack;

	/** Submixes currently overridden by the active profile (so we know what to restore on change). */
	UPROPERTY()
	TArray<TObjectPtr<USoundSubmix>> AppliedSubmixes;

	/** Index into Stack of the current active snapshot, or INDEX_NONE. */
	int32 ActiveIndex = INDEX_NONE;

	/** Monotonic push counter for tie-breaking. */
	int64 NextSequence = 1;

	/** Recompute which snapshot is active and (re)apply/restore submix volumes accordingly. */
	void RefreshActive();

	/** Apply the active profile's submix overrides; restore any previously-applied submix it omits. */
	void ApplySnapshot(const FAudio_ActiveMixSnapshot* Snapshot);
};
