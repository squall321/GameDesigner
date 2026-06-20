// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native anchor tags for the DesignPatternsHUD module's LAYOUT + NOTIFICATION area.
 *
 * These extend the module's single HUDTags namespace (C++ namespaces are open) with the tags the
 * data-driven HUD layout and the toast/banner notification queue reference literally at runtime:
 *  - the default viewport layer HUD slots are placed on;
 *  - the LOCAL/COSMETIC message-bus channels the layout + notification subsystems listen on (notify,
 *    dismiss, layout rebuild, slot show/hide) — all anchored under the core DP.Bus root so producers
 *    stay decoupled;
 *  - the service-locator key for an optional IHUD_NotificationSource producer;
 *  - the notification category root + default category (children classify toast/banner styling);
 *  - the data-asset identity roots for HUD layout + notification-map assets.
 *
 * Everything designer-facing (concrete categories, per-game slots, per-game map rules) flows through
 * data assets / settings as opaque FGameplayTag values and is NEVER hard-coded here. The full tag
 * strings are defined in HUD_HudNotifyTags.cpp.
 */
namespace HUDTags
{
	/** Default viewport layer the HUD layout pushes slot widgets onto when a slot omits a layer. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(UI_Layer_HUD);

	/** Bus channel: a producer requests a notification be shown (payload FHUD_NotificationBusPayload). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HUD_Notify);

	/** Bus channel: a producer requests an active notification (by dedupe key) be dismissed. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HUD_Dismiss);

	/** Bus channel: request the HUD layout be rebuilt (e.g. after a layout-asset swap). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HUD_LayoutRebuild);

	/** Bus channel: request a HUD slot be shown by tag (payload FHUD_SlotVisibilityBusPayload). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HUD_SlotShow);

	/** Bus channel: request a HUD slot be hidden by tag (payload FHUD_SlotVisibilityBusPayload). */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_HUD_SlotHide);

	/** Service-locator key under which a game can publish a custom IHUD_NotificationSource producer. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_NotificationSource);

	/** Notification category root; concrete categories (Info/Warning/Reward...) are children. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Notify);

	/** Default notification category used when a producer leaves Category unset. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Notify_Info);

	/** Data-asset identity root for HUD layout assets; concrete layouts are children. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_Layout);

	/** Data-asset identity root for HUD notification-map assets; concrete maps are children. */
	DESIGNPATTERNSHUD_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_NotificationMap);
}
