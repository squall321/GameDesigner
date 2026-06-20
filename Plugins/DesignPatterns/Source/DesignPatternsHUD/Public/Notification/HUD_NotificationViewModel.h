// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Notification/HUD_NotificationTypes.h"
#include "HUD_NotificationViewModel.generated.h"

/**
 * One active toast/banner as projected to the UI: the source notification plus a stable instance id
 * and the remaining on-screen time. The view renders an array of these.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_ActiveNotificationView
{
	GENERATED_BODY()

	/** Monotonic instance id assigned by the subsystem; stable for the life of this active item. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Notification")
	int64 InstanceId = 0;

	/** The notification content/metadata being displayed. */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Notification")
	FHUD_Notification Notification;

	/** Seconds remaining before auto-dismiss; <= 0 when sticky (Notification.Duration <= 0). */
	UPROPERTY(BlueprintReadOnly, Category = "HUD|Notification")
	float TimeRemaining = 0.f;
};

/**
 * ViewModel the notification UI binds to (built on the engine FieldNotification system via
 * UDP_ViewModelBase, NOT the optional MVVM plugin).
 *
 * The notification subsystem owns this object and pushes the current visible set into it; the
 * ViewModel raises field-changed notifications so any bound UDP_ViewBase re-reads. The ViewModel
 * holds NO gameplay pointers and never reaches into the world — it is a pure projection.
 *
 * Observable fields:
 *  - VisibleNotifications : the ordered list currently on screen (drives the toast/banner list).
 *  - VisibleCount         : convenience count for empty-state binding.
 *  - QueuedCount          : how many notifications are waiting behind the on-screen cap.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Notification ViewModel"))
class DESIGNPATTERNSHUD_API UHUD_NotificationViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		VisibleNotifications = 0,
		VisibleCount,
		QueuedCount,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/**
	 * Replace the visible set (called by the subsystem after every queue change). Updates
	 * VisibleNotifications + VisibleCount and broadcasts both. The subsystem also pushes QueuedCount
	 * via SetQueuedCount.
	 */
	void SetVisibleNotifications(const TArray<FHUD_ActiveNotificationView>& InVisible);

	/** Update the count of notifications waiting behind the on-screen cap; broadcasts on change. */
	void SetQueuedCount(int32 InQueuedCount);

	// --- Observable getters ---

	/** The notifications currently on screen, in display order (copied for BP safety). */
	UFUNCTION(BlueprintPure, Category = "HUD|Notification")
	TArray<FHUD_ActiveNotificationView> GetVisibleNotifications() const { return VisibleNotifications; }

	/** Number of notifications currently on screen. */
	UFUNCTION(BlueprintPure, Category = "HUD|Notification")
	int32 GetVisibleCount() const { return VisibleNotifications.Num(); }

	/** Number of notifications queued behind the on-screen cap. */
	UFUNCTION(BlueprintPure, Category = "HUD|Notification")
	int32 GetQueuedCount() const { return QueuedCount; }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Backing storage: the visible notifications in display order. */
	UPROPERTY(Transient)
	TArray<FHUD_ActiveNotificationView> VisibleNotifications;

	/** Backing storage: count of queued-but-not-yet-shown notifications. */
	UPROPERTY(Transient)
	int32 QueuedCount = 0;
};
