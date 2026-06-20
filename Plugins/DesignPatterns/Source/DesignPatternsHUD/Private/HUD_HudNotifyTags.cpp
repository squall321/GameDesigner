// Copyright DesignPatterns plugin. All Rights Reserved.

#include "HUD_HudNotifyTags.h"

namespace HUDTags
{
	// Default viewport layer (shared with the core UI layer roots under DP.UI.Layer).
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(UI_Layer_HUD, "DP.UI.Layer.HUD",
		"Default viewport layer the HUD layout pushes slot widgets onto when a slot omits a layer.");

	// LOCAL/COSMETIC bus channels under the core DP.Bus root.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HUD_Notify, "DP.Bus.HUD.Notify",
		"A producer requests a notification be shown (payload FHUD_NotificationBusPayload).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HUD_Dismiss, "DP.Bus.HUD.Dismiss",
		"A producer requests an active notification (by dedupe key) be dismissed.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HUD_LayoutRebuild, "DP.Bus.HUD.LayoutRebuild",
		"Request the HUD layout be rebuilt (e.g. after a layout-asset swap).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HUD_SlotShow, "DP.Bus.HUD.SlotShow",
		"Request a HUD slot be shown by tag (payload FHUD_SlotVisibilityBusPayload).");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Bus_HUD_SlotHide, "DP.Bus.HUD.SlotHide",
		"Request a HUD slot be hidden by tag (payload FHUD_SlotVisibilityBusPayload).");

	// Service-locator key under the core DP.Service root.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Service_NotificationSource, "DP.Service.HUD.NotificationSource",
		"Key under which a game can publish a custom IHUD_NotificationSource producer.");

	// Notification category root + default under this module's DP.HUD root.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Notify, "DP.HUD.Notify",
		"Notification category root; concrete categories (Info/Warning/Reward...) are children.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Notify_Info, "DP.HUD.Notify.Info",
		"Default notification category used when a producer leaves Category unset.");

	// Data-asset identity roots under the core DP.Data root.
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_Layout, "DP.Data.HUD.Layout",
		"Data-asset identity root for HUD layout assets; concrete layouts are children.");

	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Data_NotificationMap, "DP.Data.HUD.NotificationMap",
		"Data-asset identity root for HUD notification-map assets; concrete maps are children.");
}
