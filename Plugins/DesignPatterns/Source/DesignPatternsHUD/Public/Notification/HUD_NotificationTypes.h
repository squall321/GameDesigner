// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "HUD_NotificationTypes.generated.h"

/**
 * A single HUD notification (toast / banner) request.
 *
 * Producers push these (directly through the notification subsystem or indirectly via the message
 * bus + notification map). The subsystem queues them by Priority, de-dupes by DedupeKey, and
 * surfaces the active set through a ViewModel the UI binds. Purely LOCAL/COSMETIC — never replicated.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_Notification
{
	GENERATED_BODY()

	/**
	 * Classification tag (child of DP.HUD.Notify) the view uses to pick styling — Info / Warning /
	 * Reward / etc. When left unset the subsystem substitutes DP.HUD.Notify.Info.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Notification",
		meta = (Categories = "DP.HUD.Notify"))
	FGameplayTag Category;

	/** Primary headline text of the notification. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Notification")
	FText Title;

	/** Secondary / body text. May be empty for a title-only toast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Notification")
	FText Body;

	/**
	 * How long (seconds) the notification stays on screen once shown. A value <= 0 means "sticky":
	 * it remains until explicitly dismissed (by DedupeKey) or evicted by a higher-priority item.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Notification")
	float Duration = 0.f;

	/**
	 * Relative importance. Higher shows first and evicts lower-priority items when the on-screen cap
	 * is reached. Ties break by insertion order (FIFO).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Notification")
	int32 Priority = 0;

	/**
	 * Optional de-duplication key. While a notification with this key is active or queued, a new push
	 * with the same key REPLACES it (refreshing its text/duration) instead of stacking a duplicate.
	 * An invalid (empty) key disables de-duplication for that notification.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Notification",
		meta = (Categories = "DP.HUD"))
	FGameplayTag DedupeKey;

	FHUD_Notification() = default;

	/** True if this notification carries any displayable content. */
	bool HasContent() const
	{
		return !Title.IsEmpty() || !Body.IsEmpty();
	}
};

/**
 * Bus payload a producer broadcasts on DP.Bus.HUD.Notify to request a notification without a hard
 * dependency on the notification subsystem. Carries the full notification request inline.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_NotificationBusPayload
{
	GENERATED_BODY()

	/** The notification to surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Notification")
	FHUD_Notification Notification;
};

/**
 * Bus payload broadcast on DP.Bus.HUD.Dismiss to request the active notification matching DedupeKey
 * be removed early.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_NotificationDismissBusPayload
{
	GENERATED_BODY()

	/** The dedupe key identifying the notification(s) to dismiss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Notification",
		meta = (Categories = "DP.HUD"))
	FGameplayTag DedupeKey;
};

/**
 * Bus payload broadcast on DP.Bus.HUD.SlotShow / DP.Bus.HUD.SlotHide to request a HUD layout slot be
 * shown/hidden by tag, decoupling gameplay producers from the layout subsystem.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_SlotVisibilityBusPayload
{
	GENERATED_BODY()

	/** The HUD layout slot to show or hide (e.g. DP.HUD.Slot.HealthBar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD|Layout",
		meta = (Categories = "DP.HUD"))
	FGameplayTag SlotTag;
};
