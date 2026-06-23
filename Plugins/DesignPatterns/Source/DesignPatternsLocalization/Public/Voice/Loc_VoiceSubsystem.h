// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/WeakInterfacePtr.h"
#include "Engine/StreamableManager.h"
#include "Loc_VoiceSubsystem.generated.h"

class ULoc_VoiceBankDataAsset;
class USoundBase;
class ISeam_LipSync;
struct FLoc_VoiceLineRow;

/**
 * GameInstance-scoped per-culture VO playback + caption/lip-sync orchestration.
 *
 * RESPONSIBILITIES (all ADDITIVE — nothing here changes the base subtitle/localization subsystems):
 *  - CULTURE-AWARE BANK SELECTION: binds ULoc_LocalizationSubsystem::OnCultureChanged and selects the
 *    ULoc_VoiceBankDataAsset whose Culture matches the active culture (exact, then language-prefix). Banks
 *    are discovered through the asset manager by the voice-bank PrimaryAssetType (no hard asset refs).
 *  - ASYNC-CORRECT CAPTION TIMING: PlayLine async-loads the clip via FStreamableManager and, only AFTER
 *    the load completes, reads USoundBase::GetDuration() and broadcasts an FLoc_SubtitleLine (with the
 *    concrete duration) on DPLocTags::Bus_VoiceLine — which the base subtitle subsystem already listens on.
 *    This fixes caption/audio desync (duration is never guessed before the clip is loaded).
 *  - LIP-SYNC: routes the playing clip + optional baked curve to the ISeam_LipSync seam (resolved weakly
 *    from the service locator under DPLocTags::Service_LipSync); silent no-op when no backend is present.
 *  - PUBLISHES itself under DPLocTags::Service_Voice (weak-observed) so other modules can request VO via a
 *    resolved TScriptInterface-free getter without hard-including this module.
 *
 * AUDIO PLAYBACK NOTE: this subsystem ORCHESTRATES (load, time, caption, lip-sync); it does not itself
 * spawn an audio component (that is world/actor concern). It plays the clip via UGameplayStatics 2D audio
 * for non-positional VO, which is the correct wrapper for localized narration; positional VO is the
 * producer's job. Players hear localized lines; nothing replicates (VO is local presentation).
 *
 * GC / LIFETIME: GameInstance subsystem. The lip-sync backend is held WEAK (TWeakInterfacePtr, re-resolved
 * + pruned on use). Pending async loads are tracked by TSharedPtr<FStreamableHandle> and cancelled on
 * Deinitialize. The culture-changed binding is removed on Deinitialize.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_VoiceSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Play the VO line LineId attributed to Speaker on the active culture's bank. Async-loads the clip; on
	 * load completion plays it (2D), surfaces its caption with the clip's real duration, and starts lip-sync.
	 *
	 * @param WorldContextObject Any object with a world (to play 2D audio in the right world).
	 * @param LineId             Opaque designer line id resolved against the active culture's bank.
	 * @param Speaker            Speaker tag for caption attribution + lip-sync routing. May be empty.
	 * @return A play handle (>0) the caller can pass to StopLine, or 0 if the line could not be resolved.
	 */
	UFUNCTION(BlueprintCallable, Category = "Localization|Voice", meta = (WorldContext = "WorldContextObject"))
	int64 PlayLine(const UObject* WorldContextObject, FGameplayTag LineId, FGameplayTag Speaker);

	/**
	 * Stop a line started by PlayLine: cancels a still-pending load, ends its lip-sync pass, and clears its
	 * caption by speaker. No-op for an unknown/已-finished handle.
	 */
	UFUNCTION(BlueprintCallable, Category = "Localization|Voice")
	void StopLine(int64 Handle);

	/** The culture code of the currently selected voice bank, or empty if none selected. */
	UFUNCTION(BlueprintPure, Category = "Localization|Voice")
	FString GetActiveBankCulture() const;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Handler bound to ULoc_LocalizationSubsystem::OnCultureChanged: reselects the active bank. */
	UFUNCTION()
	void HandleCultureChanged(const FString& NewCulture);

	/** Discover all voice banks via the asset manager and pick the one matching CultureCode (exact > prefix). */
	void SelectBankForCulture(const FString& CultureCode);

	/** Bind/unbind the culture-changed delegate on the localization subsystem (null-safe). */
	void BindCultureChanged();
	void UnbindCultureChanged();

	/** Publish/unpublish this subsystem under DPLocTags::Service_Voice (weak-observed). */
	void PublishToServiceLocator();
	void UnpublishFromServiceLocator();

	/** Resolve the lip-sync backend weakly from the service locator (null if none/stale); prune on use. */
	ISeam_LipSync* ResolveLipSync() const;

	/**
	 * Bookkeeping for one in-flight or playing line. Not a UPROPERTY: the streamable handle and tags hold
	 * no UObject refs that need GC tracking (the loaded sound is owned by the streamable manager + audio).
	 */
	struct FActiveLine
	{
		int64 Handle = 0;
		FGameplayTag LineId;
		FGameplayTag Speaker;
		TSharedPtr<FStreamableHandle> LoadHandle;
		TWeakObjectPtr<const UObject> WorldContext;
		bool bStarted = false;
	};

	/** Called on the game thread when an async clip load completes; plays + captions + lip-syncs. */
	void OnLineLoaded(int64 Handle);

	/** Surface the caption for Row (with ResolvedDuration baked in) on the voice-line bus channel. */
	void SurfaceCaption(const UObject* WorldContextObject, const FLoc_VoiceLineRow& Row, float ResolvedDuration);

	/** Begin a lip-sync pass for Row's clip + optional curve via the seam (null-safe). */
	void BeginLipSyncFor(const FLoc_VoiceLineRow& Row, FGameplayTag Speaker, USoundBase* LoadedSound, UObject* LoadedCurve);

	/** Find an active line by handle, or null. */
	FActiveLine* FindActive(int64 Handle);

	/** The currently selected per-culture voice bank (owning ref while selected). */
	UPROPERTY(Transient)
	TObjectPtr<ULoc_VoiceBankDataAsset> ActiveBank = nullptr;

	/** Streamable manager for async clip/curve loads. Owned by value (it is a plain manager, not a UObject). */
	FStreamableManager Streamable;

	/** In-flight / playing lines keyed implicitly by Handle (searched linearly — counts are small). */
	TArray<FActiveLine> ActiveLines;

	/** Weakly-held lip-sync backend resolved from the locator; inert (silent) when unset. */
	TWeakInterfacePtr<ISeam_LipSync> CachedLipSync;

	/** Monotonic play-handle source. */
	int64 NextHandle = 1;

	/** True once Initialize has bound the culture delegate (so teardown unbinds exactly once). */
	bool bCultureBound = false;
};
