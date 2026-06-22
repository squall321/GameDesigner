// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsPlatform module.
 *
 * These anchor the service-locator keys under which the Platform subsystems publish their seams, plus
 * the data-asset family roots and the cosmetic bus channels the module broadcasts on. Anchoring the
 * roots in C++ guarantees the hierarchy exists at startup so designers can author child tags (feature
 * flags, haptic-effect tags, action tags, glyph banks) in the project tag config and have them match.
 *
 * - DP.Service.Platform.* : service-locator keys for the four implemented seams.
 * - DP.Data.Platform.*    : data-asset family roots (registered via UDP_DataAsset::DataTag).
 * - DP.Bus.Platform.*     : cosmetic message-bus channels (display/cloud/glyph/online change).
 */
namespace Plat_NativeTags
{
	// ---- Service-locator keys (where the Platform subsystems publish their seams) ----

	/** Key for ISeam_HapticController (UPlat_HapticFeedbackSubsystem). */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Haptics);

	/** Key for ISeam_SafeZoneProvider (UPlat_DisplaySubsystem). */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_SafeZone);

	/** Key for ISeam_InputGlyphProvider (UPlat_InputGlyphSubsystem). */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Glyphs);

	// NOTE: the cloud-store service key (DP.Service.Save.Cloud) is OWNED by the SaveSystem module
	// (SaveX_StorageServiceKeys.h). To avoid a duplicate native-tag definition we resolve it at runtime
	// via RequestGameplayTag in UPlat_CloudSaveSubsystem rather than declaring it here.

	/** Key for ISeam_AppLifecycle adapter (registered StrongOwned by the Platform module). */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_AppLifecycle);

	// ---- Data-asset family roots ----

	/** Family root for UPlat_HapticEffectSet assets. */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_HapticSet);

	/** Family root for UPlat_GlyphSet assets. */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_GlyphSet);

	/** Family root for UPlat_ScalabilityProfile assets. */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Data_ScalabilityProfile);

	// ---- Cosmetic bus channels ----

	/** Broadcast when the resolved display metrics change (resolution / DPI / orientation / safe-area). */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_DisplayChanged);

	/** Broadcast when a slot's cloud sync state changes. */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_CloudStateChanged);

	/** Broadcast when the active input-glyph family changes. */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_GlyphFamilyChanged);

	/** Broadcast when online/store/presence availability changes. */
	DESIGNPATTERNSPLATFORM_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_OnlineStateChanged);
}
