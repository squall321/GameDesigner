// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "UObject/WeakInterfacePtr.h"
#include "Containers/Ticker.h"
#include "Loc/Seam_AccessibilityConsumer.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Subtitle/Loc_SubtitleTypes.h"
#include "MessageBus/DPMessage.h"
#include "Loc_SubtitleSubsystem.generated.h"

class ULoc_SubtitleViewModel;
class UDP_MessageBusSubsystem;
class ISeam_TextToSpeech;
struct FDP_Message;
struct FLoc_ActiveSubtitleView;

/**
 * GameInstance-scoped subtitle / caption manager.
 *
 * Responsibilities:
 *  - PRIORITY QUEUE of subtitle lines: higher-priority lines show first; the on-screen set is capped at
 *    MaxOnScreen (from settings) and lower-priority lines wait. FIFO within a priority.
 *  - BUS-DRIVEN surfacing: subscribes (ListenNative) to the dialogue/voice line channels and to direct
 *    show/clear request channels, turning producer events into subtitles with zero producer coupling.
 *  - DIRECT API: ShowSubtitle / ClearSubtitles / ClearSubtitlesBySpeaker for code that holds this subsystem.
 *  - DURATION: when a line has no explicit Duration, computes it from text length times the per-character
 *    pacing in ULoc_DeveloperSettings (clamped to the configured min/max).
 *  - ACCESSIBILITY: implements ISeam_AccessibilityConsumer so the (externally owned) accessibility
 *    subsystem can push the current FSeam_AccessibilityOptions. Honors bSubtitlesEnabled / SubtitleSize /
 *    bSubtitleBackground and pushes those into the ViewModel. Inert default = all-on (subtitles render)
 *    until/unless an accessibility provider pushes options.
 *  - TEXT-TO-SPEECH: when options request it AND a TTS backend is registered (resolved weakly from the
 *    service locator), routes newly-surfaced lines to ISeam_TextToSpeech::Speak. Silent no-op otherwise.
 *  - VIEWMODEL: owns a ULoc_SubtitleViewModel the UI binds; pushes the visible set + accessibility
 *    presentation whenever the queue or options change.
 *
 * LOCAL / per-machine — nothing here replicates; subtitles are presentation of already-replicated speech.
 * Independently removable: every cross-module hop is a Seams interface resolved from the service locator,
 * each with a documented inert default.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_SubtitleSubsystem : public UDP_GameInstanceSubsystem, public ISeam_AccessibilityConsumer
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISeam_AccessibilityConsumer
	/** Called by the accessibility subsystem on register and whenever any option changes. */
	virtual void OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options) override;
	//~ End ISeam_AccessibilityConsumer

	/**
	 * Surface a subtitle line directly. De-duplicates an identical in-flight line (same speaker + text),
	 * refreshing its timer instead of stacking. Inserts by priority, surfaces immediately if there is
	 * room, otherwise queues. Routes to TTS if enabled+available.
	 * @return the instance id assigned to the (new or refreshed) line, or 0 if subtitles are disabled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Localization|Subtitle")
	int64 ShowSubtitle(const FLoc_SubtitleLine& Line);

	/** Clear every active and queued subtitle immediately. */
	UFUNCTION(BlueprintCallable, Category = "Localization|Subtitle")
	void ClearSubtitles();

	/**
	 * Clear every active/queued subtitle whose Speaker matches Speaker (hierarchy-aware: clearing
	 * DP.Loc.Speaker.NPC removes DP.Loc.Speaker.NPC.Guard). No-op if none match.
	 * @return number of lines cleared.
	 */
	UFUNCTION(BlueprintCallable, Category = "Localization|Subtitle")
	int32 ClearSubtitlesBySpeaker(FGameplayTag Speaker);

	/** The ViewModel the UI binds to (always valid after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Localization|Subtitle")
	ULoc_SubtitleViewModel* GetViewModel() const { return ViewModel; }

	/** Whether subtitles are currently allowed to render (mirrors the accessibility option). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Localization|Subtitle")
	bool AreSubtitlesEnabled() const { return CurrentOptions.bSubtitlesEnabled; }

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** A queued or active subtitle line with its bookkeeping. */
	struct FSubtitleItem
	{
		/** Monotonic instance id. */
		int64 InstanceId = 0;
		/** The line content (Duration already resolved to a concrete value). */
		FLoc_SubtitleLine Line;
		/** Seconds left on screen (only meaningful while active). */
		float TimeRemaining = 0.f;
		/** True while this line is currently on screen (counted against the on-screen cap). */
		bool bActive = false;
	};

	/** Resolve the game-instance message bus, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Resolve the configured on-screen cap from settings (defensive fallback when no CDO). */
	int32 ResolveMaxOnScreen() const;

	/** Resolve an explicit/computed duration for a line per settings (defensive fallbacks documented). */
	float ResolveDuration(const FLoc_SubtitleLine& Line) const;

	/** Map a (possibly empty) priority tag to a sortable rank; higher shows first. */
	int32 ResolvePriorityRank(const FGameplayTag& Priority) const;

	/** Subscribe to dialogue/voice line channels + direct show/clear channels. */
	void SubscribeBus();

	/** Bus handler for a dialogue/voice/show channel: extracts an FLoc_SubtitleLine and shows it. */
	void HandleLineMessage(const FDP_Message& Message);

	/** Bus handler for the clear channel: empty payload clears all; a tag payload clears by speaker. */
	void HandleClearMessage(const FDP_Message& Message);

	/** Insert Item ordered by descending priority rank (stable within a rank). */
	void InsertByPriority(FSubtitleItem&& Item);

	/** Find an active/queued item matching (Speaker,Text); null if none. Used for de-dup. */
	FSubtitleItem* FindDuplicate(const FLoc_SubtitleLine& Line);

	/** Promote queued items into the active set up to the cap; starts their timers; routes new ones to TTS. */
	void PromoteQueued();

	/** Per-frame tick: decrement active durations and dismiss expired lines. */
	bool TickSubtitles(float DeltaTime);

	/** Rebuild the ViewModel's visible set from the current item list. */
	void PushVisibleToViewModel();

	/** Push current accessibility presentation flags into the ViewModel. */
	void PushAccessibilityToViewModel();

	/** Resolve the registered TTS backend weakly (null if none / dead); prune stale on use. */
	ISeam_TextToSpeech* ResolveTTS() const;

	/** Speak Line through the TTS seam if options request it and a backend is available. */
	void MaybeSpeak(const FLoc_SubtitleLine& Line);

	/** The ViewModel the UI binds to. Owning ref keeps it alive. */
	UPROPERTY()
	TObjectPtr<ULoc_SubtitleViewModel> ViewModel = nullptr;

	/**
	 * All subtitle lines, ordered by descending priority (index 0 = highest). The first up-to-cap items
	 * flagged bActive are on screen; the rest are queued. Not a UPROPERTY: holds no UObject refs (the
	 * line is a plain value struct), so GC has nothing to track.
	 */
	TArray<FSubtitleItem> Items;

	/**
	 * Current accessibility options. Defaults to the struct's all-on defaults (subtitles enabled, medium
	 * size, background on) so the system renders even when no accessibility provider ever pushes options.
	 */
	FSeam_AccessibilityOptions CurrentOptions;

	/**
	 * Weak, null-checked reference to the resolved TTS backend. Held weakly (TWeakInterfacePtr) so a TTS
	 * provider owned elsewhere cannot be kept alive by this subsystem across worlds; re-resolved + pruned
	 * on use.
	 */
	TWeakInterfacePtr<ISeam_TextToSpeech> CachedTTS;

	/** Bus listener handles so we can stop listening cleanly on Deinitialize. */
	TArray<FDP_ListenerHandle> ListenerHandles;

	/** Monotonic instance-id source. */
	int64 NextInstanceId = 1;

	/** FTSTicker handle draining subtitle timers each frame. */
	FTSTicker::FDelegateHandle TickerHandle;
};
