// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Loc/Seam_AccessibilityConsumer.h"
#include "Loc/Seam_AccessibilityTypes.h"
#include "Subtitle/Loc_RichSubtitleTypes.h"
#include "MessageBus/DPMessage.h"
#include "Loc_RichSubtitleSubsystem.generated.h"

class ULoc_SpeakerStyleDataAsset;
class ULoc_SubtitleHistoryViewModel;
class UDP_MessageBusSubsystem;
struct FDP_Message;

/**
 * GameInstance-scoped RICH-subtitle observer + backlog. ADDITIVE — it does NOT modify or replace
 * ULoc_SubtitleSubsystem; it observes the SAME line channels and adds speaker-styling, accessibility
 * styling, and a persistent history backlog.
 *
 * RESPONSIBILITIES:
 *  - Subscribes (ListenNative) to the FLoc_SubtitleLine-carrying channels (Bus_DialogueLine / Bus_VoiceLine
 *    / Bus_SubtitleShow) for line CONTENT — NOT Bus_SubtitleChanged (that signals visible-set change only).
 *  - Resolves each line's per-speaker style from ULoc_SpeakerStyleDataAsset and applies accessibility
 *    corrections (colorblind remap of the name color, size/background flags) using the current
 *    FSeam_AccessibilityOptions pushed via ISeam_AccessibilityConsumer.
 *  - Maintains a settings-capped history ring buffer and pushes it (+ unread count) to a
 *    ULoc_SubtitleHistoryViewModel the backlog UI binds.
 *
 * Implements ISeam_AccessibilityConsumer so the accessibility subsystem pushes options on register/change.
 * Registers itself with the accessibility provider (resolved from the locator) on Initialize.
 *
 * LOCAL / per-machine — nothing replicates (subtitles are presentation). GC: GameInstance subsystem; the
 * history VM is an owning subobject; the speaker-style asset is loaded + held with an owning TObjectPtr;
 * bus listeners + the accessibility registration are removed on Deinitialize.
 */
UCLASS()
class DESIGNPATTERNSLOCALIZATION_API ULoc_RichSubtitleSubsystem : public UDP_GameInstanceSubsystem, public ISeam_AccessibilityConsumer
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin ISeam_AccessibilityConsumer
	virtual void OnAccessibilityOptionsChanged_Implementation(const FSeam_AccessibilityOptions& Options) override;
	//~ End ISeam_AccessibilityConsumer

	/** Resolve the full presentation style for a line (speaker style + accessibility corrections). */
	UFUNCTION(BlueprintCallable, Category = "Localization|Subtitle")
	FLoc_ResolvedSubtitleStyle ResolveStyle(const FLoc_SubtitleLine& Line) const;

	/** Get up to MaxEntries most-recent history entries (newest-last). <=0 returns all. */
	UFUNCTION(BlueprintCallable, Category = "Localization|Subtitle")
	TArray<FLoc_SubtitleHistoryEntry> GetHistory(int32 MaxEntries) const;

	/** Clear the entire backlog and reset unread count. */
	UFUNCTION(BlueprintCallable, Category = "Localization|Subtitle")
	void ClearHistory();

	/** The history ViewModel the backlog UI binds (always valid after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Localization|Subtitle")
	ULoc_SubtitleHistoryViewModel* GetHistoryViewModel() const { return HistoryViewModel; }

	/** Set the speaker-style asset to resolve styles from (also accepted via settings DataTag at init). */
	UFUNCTION(BlueprintCallable, Category = "Localization|Subtitle")
	void SetSpeakerStyleAsset(ULoc_SpeakerStyleDataAsset* InAsset);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Resolve the game-instance message bus, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Subscribe to the FLoc_SubtitleLine-carrying line channels. */
	void SubscribeBus();

	/** Bus handler: extract an FLoc_SubtitleLine, resolve its style, push it into the backlog. */
	void HandleLineMessage(const FDP_Message& Message);

	/** Register this subsystem as an accessibility consumer with the resolved provider (null-safe). */
	void RegisterWithAccessibilityProvider();

	/** Unregister from the accessibility provider (null-safe). */
	void UnregisterFromAccessibilityProvider();

	/** Add Entry to the backlog, enforce the cap, bump unread, and push to the VM. */
	void AppendHistory(const FLoc_SubtitleHistoryEntry& Entry);

	/** Resolve the configured history cap from settings (defensive fallback). */
	int32 ResolveHistoryCap() const;

	/** The backlog history ring (newest-last). Plain value structs (no UObject refs). */
	TArray<FLoc_SubtitleHistoryEntry> History;

	/** Unread entries since the last MarkAllRead on the VM. */
	int32 UnreadCount = 0;

	/** The history ViewModel the UI binds (owning ref). */
	UPROPERTY(Transient)
	TObjectPtr<ULoc_SubtitleHistoryViewModel> HistoryViewModel = nullptr;

	/** The speaker-style asset (owning ref while held). */
	UPROPERTY(Transient)
	TObjectPtr<ULoc_SpeakerStyleDataAsset> SpeakerStyles = nullptr;

	/** Current accessibility options (defaults to all-on so styling works before any provider pushes). */
	FSeam_AccessibilityOptions CurrentOptions;

	/** Bus listener handles, removed on Deinitialize. */
	TArray<FDP_ListenerHandle> ListenerHandles;

	/** True once we registered as an accessibility consumer (so teardown unregisters exactly once). */
	bool bRegisteredAccessibility = false;
};
