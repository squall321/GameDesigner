// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Item/RPG_DepthTags.h"

namespace RPG_DepthTags
{
	UE_DEFINE_GAMEPLAY_TAG(Rarity, "RPG.Rarity");
	UE_DEFINE_GAMEPLAY_TAG(Socket, "RPG.Socket");

	UE_DEFINE_GAMEPLAY_TAG(StatSource_Equip, "RPG.StatSource.Equip");
	UE_DEFINE_GAMEPLAY_TAG(StatSource_Set, "RPG.StatSource.Set");
	UE_DEFINE_GAMEPLAY_TAG(StatSource_Encumbrance, "RPG.StatSource.Encumbrance");

	UE_DEFINE_GAMEPLAY_TAG(Attribute_MoveSpeedMult, "RPG.Attribute.MoveSpeedMult");
	UE_DEFINE_GAMEPLAY_TAG(Attribute_Strength, "RPG.Attribute.Strength");

	UE_DEFINE_GAMEPLAY_TAG(Persist_ItemInstances, "RPG.Persist.ItemInstances");

	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_ItemCrafted, "DP.Bus.RPG.ItemCrafted");
	UE_DEFINE_GAMEPLAY_TAG(Bus_RPG_EncumbranceChanged, "DP.Bus.RPG.EncumbranceChanged");
}
