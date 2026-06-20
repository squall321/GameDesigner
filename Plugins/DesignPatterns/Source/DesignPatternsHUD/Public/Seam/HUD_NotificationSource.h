// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Notification/HUD_NotificationTypes.h"
#include "HUD_NotificationSource.generated.h"

class UHUD_NotificationSubsystem;

UINTERFACE(BlueprintType, MinimalAPI, meta = (CannotImplementInterfaceInBlueprint = false))
class UHUD_NotificationSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Push-only producer contract for HUD notifications.
 *
 * A notification SOURCE is anything (a gameplay component, a quest system, an analytics relay)
 * that wants to emit toasts/banners WITHOUT taking a hard dependency on the HUD subsystem. The
 * notification subsystem owns the sink side; a source is "pulled" once at registration so it can
 * hand the subsystem a binding it pushes through later.
 *
 * The cleaner, fully decoupled path is the message bus (broadcast FHUD_NotificationBusPayload on
 * DP.Bus.HUD.Notify). This seam exists for producers that prefer a direct, typed push registered
 * via the service locator (DP.Service.HUD.NotificationSource) — e.g. when ordering/back-pressure
 * matters and the bus's tag-fan-out is undesirable.
 *
 * LOCAL ONLY: notifications are cosmetic; a source must already run on the local client (it is
 * driven by replicated state / OnRep), never produce authoritative state here.
 */
class DESIGNPATTERNSHUD_API IHUD_NotificationSource
{
	GENERATED_BODY()

public:
	/**
	 * Called by the notification subsystem when this source becomes active (on registration), handing
	 * the source a weak pointer to the sink it should push to. Implementations typically cache it and
	 * bind their own gameplay delegates that later call Sink->PushNotification(...).
	 *
	 * @param Sink  The notification subsystem to push into. Hold WEAKLY — do not keep it alive.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "HUD|Notification")
	void OnNotificationSinkBound(UHUD_NotificationSubsystem* Sink);

	/**
	 * Called when this source is being detached (subsystem teardown / explicit unregister) so it can
	 * unbind its gameplay delegates and drop its cached sink pointer.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "HUD|Notification")
	void OnNotificationSinkUnbound();

	/**
	 * Optional immediate drain: the subsystem calls this once after binding so a source can flush any
	 * notifications it had buffered before the sink existed. Append to OutPending; the subsystem
	 * enqueues them in order. Default (via _Implementation) appends nothing.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "HUD|Notification")
	void DrainPendingNotifications(TArray<FHUD_Notification>& OutPending);
};
