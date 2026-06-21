// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native anchor + leaf tags for the RPG additive-depth layer (rarity, sockets, stat-modifier sources and
 * the derived attributes the equipment / encumbrance systems drive).
 *
 * Concrete content (specific rarities, socket types, attributes) is still authored as child tags by the
 * project; the roots here guarantee the hierarchy exists at startup so SourceTag-keyed modifier groups and
 * tag-hierarchy matching always resolve. A small set of well-known SOURCE and ATTRIBUTE leaves is anchored
 * natively because the C++ systems below reference them directly (e.g. the encumbrance move-speed source).
 */
namespace RPG_DepthTags
{
	// --- Rarity ---
	/** Root for item rarity tiers (project authors RPG.Rarity.Common, .Rare, .Legendary, ...). */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Rarity);

	// --- Sockets ---
	/** Root for socket type tags (project authors RPG.Socket.Gem, .Rune, ...). */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Socket);

	// --- Stat-modifier source roots (keys used with ISeam_StatModifierSink::SetDerivedModifierGroup) ---
	/** Source root for modifiers derived from equipped gear (affixes/sockets), per slot below it. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(StatSource_Equip);
	/** Source for modifiers derived from active equipment-set bonuses. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(StatSource_Set);
	/** Source for the over-encumbrance movement penalty. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(StatSource_Encumbrance);

	// --- Well-known attributes referenced by the depth systems ---
	/** Multiplicative move-speed attribute the encumbrance system contributes a penalty to. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attribute_MoveSpeedMult);
	/** Strength attribute read to scale carry capacity (optional; absent = base capacity only). */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Attribute_Strength);

	// --- Persistence record kinds (ISeam_Persistable::GetPersistenceKind) ---
	/** Record kind for the per-instance item carrier. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_ItemInstances);

	// --- Bus channels for RPG depth feedback (children of the core DP.Bus root) ---
	/** Broadcast (cosmetic) when an item instance is upgraded/enchanted/salvaged. */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_ItemCrafted);
	/** Broadcast when the encumbrance tier changes (UI/SFX). */
	DESIGNPATTERNSRPG_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_RPG_EncumbranceChanged);
}
