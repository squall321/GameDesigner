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

	/**
	 * ADDITIVE: per-push fade-time override (seconds). < 0 means "use each submix override's own
	 * FadeTime" (the shipped default). >= 0 overrides every blend this snapshot drives — used by the
	 * blended dynamic-mixing path and by reverb-zone submix-effect wet-level fades. Defaulting to -1
	 * keeps every previously-authored push byte-identical in behaviour.
	 */
	UPROPERTY()
	float BlendTimeOverride = -1.f;
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

	// ------------------------------------------------------------------------------------------------
	//  ADDITIVE deepening (dynamic mixing depth). New public API only; nothing above is changed.
	// ------------------------------------------------------------------------------------------------

	/**
	 * Push a profile onto the stack like PushProfile, but override the FADE TIME used when this push
	 * (or any subsequent active change it causes) applies its submix overrides. This lets a transient
	 * priority duck (e.g. dialogue over music) blend in/out faster or slower than the profile authored.
	 *
	 * A negative BlendTimeOverride means "use each submix override's own FadeTime" (identical to
	 * PushProfile). PriorityOverride behaves exactly as on PushProfile (>= 0 replaces the profile's
	 * own Priority; < 0 keeps it).
	 *
	 * @return a pop handle (invalid if Profile is null), released via PopProfile.
	 */
	FGuid PushProfileBlended(UAudio_MixProfileDataAsset* Profile, float BlendTimeOverride, int32 PriorityOverride = -1);

	/**
	 * Collect the EFFECTIVE per-category duck multiplier currently in force, keyed by each duck rule's
	 * target category, from the active profile. This is the data the sound manager uses to re-scale all
	 * live voices in one pass (instead of probing GetActiveDuckVolume per category). Categories with no
	 * active rule are omitted (callers treat an absent key as 1.0 = no duck). Cosmetic/local.
	 */
	void GetEffectiveDuckVolumes(TMap<FGameplayTag, float>& Out) const;

protected:
	/**
	 * ADDITIVE extension hook. Called from RefreshActive AFTER the submix volume snapshot has been
	 * (re)applied, with a pointer to the now-active snapshot (NULL when the stack is empty / cleared).
	 *
	 * The base implementation MEDIATES reverb-zone submix effects: it removes the effect chain of the
	 * previously-active reverb profile (if the active profile changed) and adds the new active profile's
	 * effects when it is a UAudio_ReverbMixProfileDataAsset, by calling that asset's ApplySubmixEffects
	 * (which wraps UAudioMixerBlueprintLibrary::AddSubmixEffect / RemoveSubmixEffect). Non-reverb
	 * profiles and an empty stack remove any lingering reverb effect. Plain mix profiles are unaffected.
	 *
	 * Marked virtual so a project can extend with further effect kinds. Pointer (not reference) is
	 * deliberate so the cleared/empty state is representable as nullptr.
	 */
	virtual void ApplyExtraSubmixEffects(const FAudio_ActiveMixSnapshot* Active);

private:
	/** Active pushes, unordered; the active snapshot is computed by priority+sequence each change. */
	UPROPERTY()
	TArray<FAudio_ActiveMixSnapshot> Stack;

	/** Submixes currently overridden by the active profile (so we know what to restore on change). */
	UPROPERTY()
	TArray<TObjectPtr<USoundSubmix>> AppliedSubmixes;

	/**
	 * ADDITIVE: the reverb profile whose submix effect chain is currently applied (if any), tracked so
	 * ApplyExtraSubmixEffects can REMOVE its effects when the active profile changes. Strong so it
	 * cannot be GC'd between apply and remove. Null when no reverb effect is currently applied.
	 */
	UPROPERTY()
	TObjectPtr<class UAudio_ReverbMixProfileDataAsset> AppliedReverbProfile = nullptr;

	/** Index into Stack of the current active snapshot, or INDEX_NONE. */
	int32 ActiveIndex = INDEX_NONE;

	/** Monotonic push counter for tie-breaking. */
	int64 NextSequence = 1;

	/** Recompute which snapshot is active and (re)apply/restore submix volumes accordingly. */
	void RefreshActive();

	/** Apply the active profile's submix overrides; restore any previously-applied submix it omits. */
	void ApplySnapshot(const FAudio_ActiveMixSnapshot* Snapshot);
};
