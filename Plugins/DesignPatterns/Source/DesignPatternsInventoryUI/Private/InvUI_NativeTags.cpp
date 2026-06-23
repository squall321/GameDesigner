// Copyright DesignPatterns plugin. All Rights Reserved.

#include "InvUI_NativeTags.h"

namespace InvUITags
{
	// Service-locator keys live under the core DP.Service root so the locator can list them
	// alongside every other published provider.
	UE_DEFINE_GAMEPLAY_TAG(Service_ContainerRegistry, "DP.Service.InvUI.ContainerRegistry");
	UE_DEFINE_GAMEPLAY_TAG(Service_ItemDisplay,       "DP.Service.InvUI.ItemDisplay");
	UE_DEFINE_GAMEPLAY_TAG(Service_ItemStats,         "DP.Service.InvUI.ItemStats");

	// Intent verbs under the registered DP.InvUI.Intent root.
	UE_DEFINE_GAMEPLAY_TAG(Intent_Move,  "DP.InvUI.Intent.Move");
	UE_DEFINE_GAMEPLAY_TAG(Intent_Split, "DP.InvUI.Intent.Split");
	UE_DEFINE_GAMEPLAY_TAG(Intent_Merge, "DP.InvUI.Intent.Merge");
	UE_DEFINE_GAMEPLAY_TAG(Intent_Drop,  "DP.InvUI.Intent.Drop");
	UE_DEFINE_GAMEPLAY_TAG(Intent_Use,   "DP.InvUI.Intent.Use");
	UE_DEFINE_GAMEPLAY_TAG(Intent_Equip, "DP.InvUI.Intent.Equip");
	UE_DEFINE_GAMEPLAY_TAG(Intent_Sort,  "DP.InvUI.Intent.Sort");
	UE_DEFINE_GAMEPLAY_TAG(Intent_Place, "DP.InvUI.Intent.Place");
	UE_DEFINE_GAMEPLAY_TAG(Intent_Rotate,"DP.InvUI.Intent.Rotate");

	// Capability advertisements under the registered DP.InvUI.Cap root.
	UE_DEFINE_GAMEPLAY_TAG(Cap_Move,       "DP.InvUI.Cap.Move");
	UE_DEFINE_GAMEPLAY_TAG(Cap_Split,      "DP.InvUI.Cap.Split");
	UE_DEFINE_GAMEPLAY_TAG(Cap_Sort,       "DP.InvUI.Cap.Sort");
	UE_DEFINE_GAMEPLAY_TAG(Cap_FixedSlots, "DP.InvUI.Cap.FixedSlots");
	UE_DEFINE_GAMEPLAY_TAG(Cap_ReadOnly,   "DP.InvUI.Cap.ReadOnly");

	// Screen ids under the core DP.UI.Screen root.
	UE_DEFINE_GAMEPLAY_TAG(Screen_Bag,       "DP.UI.Screen.InvUI.Bag");
	UE_DEFINE_GAMEPLAY_TAG(Screen_Equipment, "DP.UI.Screen.InvUI.Equipment");
	UE_DEFINE_GAMEPLAY_TAG(Screen_Container, "DP.UI.Screen.InvUI.Container");
	UE_DEFINE_GAMEPLAY_TAG(Screen_Vendor,    "DP.UI.Screen.InvUI.Vendor");
}
