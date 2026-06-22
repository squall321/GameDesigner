// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Plat_NativeTags.h"

namespace Plat_NativeTags
{
	// ---- Service-locator keys ----
	UE_DEFINE_GAMEPLAY_TAG(Service_Haptics, "DP.Service.Platform.Haptics");
	UE_DEFINE_GAMEPLAY_TAG(Service_SafeZone, "DP.Service.Platform.SafeZone");
	UE_DEFINE_GAMEPLAY_TAG(Service_Glyphs, "DP.Service.Platform.Glyphs");
	// Service_CloudSave (DP.Service.Save.Cloud) is owned by SaveSystem; resolved at runtime instead.
	UE_DEFINE_GAMEPLAY_TAG(Service_AppLifecycle, "DP.Service.Platform.AppLifecycle");

	// ---- Data-asset family roots ----
	UE_DEFINE_GAMEPLAY_TAG(Data_HapticSet, "DP.Data.Platform.HapticSet");
	UE_DEFINE_GAMEPLAY_TAG(Data_GlyphSet, "DP.Data.Platform.GlyphSet");
	UE_DEFINE_GAMEPLAY_TAG(Data_ScalabilityProfile, "DP.Data.Platform.ScalabilityProfile");

	// ---- Cosmetic bus channels ----
	UE_DEFINE_GAMEPLAY_TAG(Bus_DisplayChanged, "DP.Bus.Platform.DisplayChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_CloudStateChanged, "DP.Bus.Platform.CloudStateChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_GlyphFamilyChanged, "DP.Bus.Platform.GlyphFamilyChanged");
	UE_DEFINE_GAMEPLAY_TAG(Bus_OnlineStateChanged, "DP.Bus.Platform.OnlineStateChanged");
}
