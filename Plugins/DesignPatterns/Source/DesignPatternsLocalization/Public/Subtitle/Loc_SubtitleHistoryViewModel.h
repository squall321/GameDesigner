// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Subtitle/Loc_RichSubtitleTypes.h"
#include "Loc_SubtitleHistoryViewModel.generated.h"

/**
 * ViewModel a subtitle BACKLOG / history UI binds to. Built on the engine FieldNotification system via
 * UDP_ViewModelBase (NOT the optional MVVM plugin), with its OWN field descriptor — it does NOT subclass
 * the live-caption ULoc_SubtitleViewModel; the two are independent projections.
 *
 * The rich subtitle subsystem owns this object and pushes the capped history + unread count; it raises
 * field-changed notifications so a bound view re-reads. Holds NO gameplay pointers — pure projection.
 *
 * Observable fields:
 *  - HistoryEntries : the backlog lines, newest-last (drives a scrollable history view).
 *  - UnreadCount    : how many entries arrived since the player last marked history as read.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Localization Subtitle History ViewModel"))
class DESIGNPATTERNSLOCALIZATION_API ULoc_SubtitleHistoryViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		HistoryEntries = 0,
		UnreadCount,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/** Replace the history list (called by the subsystem on every backlog change). Broadcasts both fields. */
	void SetHistory(const TArray<FLoc_SubtitleHistoryEntry>& InEntries, int32 InUnreadCount);

	/** Mark the history as read (UnreadCount -> 0); broadcasts UnreadCount if it changed. */
	UFUNCTION(BlueprintCallable, Category = "Localization|Subtitle")
	void MarkAllRead();

	/** The backlog entries, newest-last (copied for BP safety). */
	UFUNCTION(BlueprintPure, Category = "Localization|Subtitle")
	TArray<FLoc_SubtitleHistoryEntry> GetHistoryEntries() const { return HistoryEntries; }

	/** Number of entries since the last MarkAllRead. */
	UFUNCTION(BlueprintPure, Category = "Localization|Subtitle")
	int32 GetUnreadCount() const { return UnreadCount; }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Backing storage: backlog entries, newest-last. */
	UPROPERTY(Transient)
	TArray<FLoc_SubtitleHistoryEntry> HistoryEntries;

	/** Backing storage: unread count since last MarkAllRead. */
	UPROPERTY(Transient)
	int32 UnreadCount = 0;
};
