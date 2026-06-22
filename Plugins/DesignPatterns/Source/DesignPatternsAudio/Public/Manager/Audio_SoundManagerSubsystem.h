// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Engine/StreamableManager.h"
#include "Core/DPSubsystemBases.h"
#include "Seam/Audio_AudioController.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4 and in CoreUObject on 5.5+. The bus payload
// structs below are carried as instanced structs on the message bus.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "Audio_SoundManagerSubsystem.generated.h"

class UAudio_SoundBankDataAsset;
class UAudio_MixController;
class UAudio_MixProfileDataAsset;
class UAudio_DuckBusDataAsset;
class UAudioComponent;
class USoundBase;
class USoundAttenuation;
struct FAudio_SoundEntry;

/**
 * Bus payload (DP.Bus.Audio.Play): request a one-shot play-by-tag without resolving the controller.
 * Built with the engine's Make Instanced Struct node and broadcast through the message bus; the
 * sound manager listens on AudioNativeTags::Bus_Play and routes it to PlaySound2D/AtLocation.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_PlayRequest
{
	GENERATED_BODY()

	/** Sound identity to play (child of DP.Audio.Sound). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (Categories = "DP.Audio.Sound"))
	FGameplayTag SoundTag;

	/** Per-call linear volume multiplier (1.0 = bank default). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "4.0"))
	float VolumeMult = 1.f;

	/** When true play spatialized at Location; otherwise play 2D and ignore Location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bAtLocation = false;

	/** World location for spatialized playback (used only when bAtLocation is true). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FVector Location = FVector::ZeroVector;
};

/**
 * Bus payload (DP.Bus.Audio.Mix): push or pop a mix profile by tag at a priority. A push returns no
 * handle through the bus (fire-and-forget); pair it with a later pop of the same MixTag (the manager
 * pops the most-recent push of that tag). For handle-precise control resolve the controller directly.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_MixRequest
{
	GENERATED_BODY()

	/** Mix profile identity (child of DP.Audio.Mix), resolved via the data registry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (Categories = "DP.Audio.Mix"))
	FGameplayTag MixTag;

	/** True = push the profile; false = pop the most-recent push of MixTag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bPush = true;

	/** Priority override for a push (>=0 replaces the profile's own Priority; <0 keeps it). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "-1", UIMin = "-1", UIMax = "100"))
	int32 PriorityOverride = -1;
};

/**
 * Bus payload (DP.Bus.Audio.CategoryVolume): set a category's runtime volume multiplier.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAUDIO_API FAudio_CategoryVolumeRequest
{
	GENERATED_BODY()

	/** Category to retune (child of DP.Audio.Category). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (Categories = "DP.Audio.Category"))
	FGameplayTag Category;

	/** New linear volume multiplier (0 = mute, 1 = unattenuated). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "2.0"))
	float Volume = 1.f;
};

/**
 * Genre-agnostic, tag-keyed sound manager. The concrete IAudio_AudioController.
 *
 * GameInstance-scoped (survives level travel) DP subsystem that:
 *   - loads the project's default sound banks (Audio_DeveloperSettings) plus any runtime-added banks;
 *   - resolves a USoundBase by sound tag across the loaded banks and ASYNC-loads it via the engine
 *     FStreamableManager (the soft sound ref), deferring the actual play to load completion;
 *   - plays through UGameplayStatics::PlaySound2D / SpawnSoundAtLocation (it WRAPS the engine, never
 *     reinvents playback) and tracks the resulting UAudioComponent as an active "voice";
 *   - enforces per-category concurrency (per-entry MaxConcurrent + per-category voice caps from
 *     settings) with VIRTUALIZATION by oldest-steal (stop the oldest voice to make room);
 *   - applies category volume multipliers and ducking (driven by the owned UAudio_MixController);
 *   - registers itself into the service locator under DP.Service.Audio (WeakObserved) and is reached
 *     by other systems only through the IAudio_AudioController seam;
 *   - listens on DP.Bus.Audio.* so gameplay can drive audio purely via already-replicated messages.
 *
 * Everything is LOCAL/COSMETIC and never replicated. Headless / no-audio-device safe: every play is
 * a guarded no-op when there is no world/audio device, and all bookkeeping degrades cleanly.
 */
UCLASS()
class DESIGNPATTERNSAUDIO_API UAudio_SoundManagerSubsystem
	: public UDP_GameInstanceSubsystem
	, public IAudio_AudioController
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin IAudio_AudioController
	virtual void PlaySound2D_Implementation(FGameplayTag SoundTag, float VolumeMult) override;
	virtual void PlaySoundAtLocation_Implementation(FGameplayTag SoundTag, FVector Location, float VolumeMult) override;
	virtual void StopCategory_Implementation(FGameplayTag Category) override;
	virtual void SetCategoryVolume_Implementation(FGameplayTag Category, float Volume) override;
	//~ End IAudio_AudioController

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/**
	 * Register an additional sound bank at runtime (beyond the settings defaults). Idempotent.
	 * Tags already present in an earlier-loaded bank keep their first binding (logged on conflict).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	void AddSoundBank(UAudio_SoundBankDataAsset* Bank);

	/** Remove a previously-added sound bank. Active voices already spawned from it keep playing. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	void RemoveSoundBank(UAudio_SoundBankDataAsset* Bank);

	/** Push a resolved mix profile by tag onto the mix controller. Returns the pop handle. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	FGuid PushMixProfile(FGameplayTag MixTag, int32 PriorityOverride = -1);

	/** Push a mix profile asset directly (skips data-registry resolution). Returns the pop handle. */
	FGuid PushMixProfileAsset(UAudio_MixProfileDataAsset* Profile, int32 PriorityOverride = -1);

	/** Pop a mix profile previously pushed (by the handle returned from a push). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Audio")
	void PopMixProfile(FGuid Handle);

	// ------------------------------------------------------------------------------------------------
	//  ADDITIVE deepening — reverb zones (2) + dynamic mixing depth (3). New API only.
	// ------------------------------------------------------------------------------------------------

	/**
	 * Push a mix profile ASSET with an explicit BLEND TIME (seconds), routed through the mix
	 * controller's blended push. Used by reverb-zone volumes (so the reverb effect fades with the
	 * zone) and by priority ducking that wants a custom blend. < 0 BlendTime uses the profile's own
	 * per-override fade times. Returns the pop handle (invalid if Profile is null).
	 */
	FGuid PushMixProfileAssetBlended(UAudio_MixProfileDataAsset* Profile, float BlendTimeSeconds, int32 PriorityOverride = -1);

	/**
	 * Resolve a duck-bus data asset (DP.Audio.Mix.Duck child) and begin priority ducking: while held,
	 * the duckee categories are scaled down (their voices re-mixed) WITHOUT pushing a full profile.
	 * Returns a handle to pass to ReleaseDuck. Used by the VO subsystem so dialogue ducks music/SFX.
	 */
	FGuid PushDuckBus(FGameplayTag DuckBusTag);

	/** Push a duck-bus data asset directly (skips registry resolution). Returns the release handle. */
	FGuid PushDuckBusAsset(class UAudio_DuckBusDataAsset* DuckBus);

	/** Release a duck previously begun by PushDuckBus/PushDuckBusAsset (no-op if unknown). */
	void ReleaseDuck(FGuid Handle);

	/** Effective owned mix controller (may be null mid-teardown). For sibling audio systems only. */
	UAudio_MixController* GetMixController() const { return MixController; }

	/** Current number of active (playing, non-virtualized) voices across all categories. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Audio")
	int32 GetActiveVoiceCount() const;

private:
	/**
	 * One tracked playing voice. Holds a weak ref to the spawned UAudioComponent (the engine owns its
	 * lifetime; it auto-destroys on finish), plus the metadata needed for concurrency/volume/ducking.
	 */
	struct FActiveVoice
	{
		/** The engine-spawned component; weak because the engine destroys it when the sound finishes. */
		TWeakObjectPtr<UAudioComponent> Component;

		/** Sound identity this voice was spawned from (for per-entry concurrency counting). */
		FGameplayTag SoundTag;

		/** Category this voice belongs to (drives category cap, volume and ducking). */
		FGameplayTag Category;

		/** The non-category part of this voice's volume (call mult * bank default * master). */
		float BaseVolume = 1.f;

		/** Monotonic spawn order; the lowest is the "oldest" stolen first during virtualization. */
		int64 Sequence = 0;
	};

	/** All currently-tracked voices. Pruned of finished/destroyed components lazily before each play. */
	TArray<FActiveVoice> ActiveVoices;

	/** Loaded sound banks (settings defaults + runtime-added), in resolution (load) order. */
	UPROPERTY()
	TArray<TObjectPtr<UAudio_SoundBankDataAsset>> LoadedBanks;

	/** Owned mix controller (instanced subobject). Drives submix snapshots + supplies duck volumes. */
	UPROPERTY()
	TObjectPtr<UAudio_MixController> MixController;

	/** Runtime per-category linear volume multipliers (seeded from settings, overridden at runtime). */
	TMap<FGameplayTag, float> CategoryVolumes;

	/** In-flight async loads keyed by sound tag, so concurrent requests for the same tag coalesce. */
	TMap<FGameplayTag, TSharedPtr<struct FStreamableHandle>> PendingLoads;

	/**
	 * Push handles created via the fire-and-forget DP.Bus.Audio.Mix channel, keyed by mix tag, so a
	 * later bus "pop by tag" can release the most-recent push of that tag (LIFO).
	 */
	TMap<FGameplayTag, TArray<FGuid>> BusMixHandles;

	/** Monotonic voice spawn sequence for oldest-steal ordering. */
	int64 NextVoiceSequence = 1;

	/** Whether this game instance has any audio device at all (false on dedicated server / -nosound). */
	bool bAudioAvailable = false;

	// ---- internals ----

	/** Resolve a sound entry by tag across LoadedBanks (first bank wins). Null if unresolved. */
	const FAudio_SoundEntry* ResolveEntry(const FGameplayTag& SoundTag) const;

	/** Effective category volume for a category (runtime table, hierarchy-walked; 1.0 fallback). */
	float ResolveCategoryVolume(const FGameplayTag& Category) const;

	/**
	 * Shared play path. Resolves the entry, ensures the soft sound is loaded (async; defers on miss),
	 * enforces concurrency, then spawns the voice. bAtLocation selects 2D vs spatialized.
	 */
	void PlayInternal(const FGameplayTag& SoundTag, float VolumeMult, bool bAtLocation, const FVector& Location);

	/**
	 * Called once a sound's async load completes (or immediately if already loaded). Performs the
	 * concurrency check and spawns the voice. Safe if the subsystem is mid-teardown (guards world).
	 */
	void OnSoundReadyAndPlay(FGameplayTag SoundTag, float BaseVolume, bool bAtLocation, FVector Location,
		TSoftObjectPtr<USoundAttenuation> Attenuation);

	/** Spawn the actual engine voice (2D or at-location) and track it. Returns the new component or null. */
	UAudioComponent* SpawnVoice(USoundBase* Sound, float Volume, bool bAtLocation, const FVector& Location,
		const FGameplayTag& SoundTag, const FGameplayTag& Category, USoundAttenuation* Attenuation);

	/**
	 * Enforce concurrency for a prospective new voice of (SoundTag, Category): if the per-entry
	 * MaxConcurrent or the per-category voice cap would be exceeded, steal (stop) the oldest matching
	 * voice. Returns true if there is room (after any steal) to spawn.
	 */
	bool MakeRoomForVoice(const FGameplayTag& SoundTag, const FGameplayTag& Category, int32 EntryMaxConcurrent);

	/** Drop tracked voices whose component has finished/been GC'd. */
	void PruneFinishedVoices();

	/** Re-apply the effective volume (base * category * duck) to every active voice in Category. */
	void RefreshCategoryVoiceVolumes(const FGameplayTag& Category);

	/** Register/unregister this subsystem as the DP.Service.Audio provider. */
	void RegisterAsService();
	void UnregisterAsService();

	/** Subscribe to the DP.Bus.Audio.* channels this manager honours. */
	void BindBusListeners();

	/** Load the configured default banks (and pre-resolve default mix profiles) from settings. */
	void LoadDefaultBanksFromSettings();
};
