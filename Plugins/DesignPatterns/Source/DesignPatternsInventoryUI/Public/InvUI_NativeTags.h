// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native anchor tags for the DesignPatternsInventoryUI module.
 *
 * These are the C++-defined roots/leaves the InventoryUI framework references by name at
 * runtime: the service-locator key under which a live container registry / item-display
 * resolver is published, the player->server *intent* verbs the (player-owned) intent
 * component routes through, the container *capability* tags a backend advertises so the
 * window UI can enable/disable affordances without knowing the backend's concrete type,
 * and the canonical inventory screen ids registered under the core `DP.UI.Screen` root.
 *
 * Game projects extend the item-kind / slot-kind hierarchies in their own tag config; this
 * module only anchors the tags its own code mentions literally. Everything designer-facing
 * (item tags, slot tags, container-kind tags) flows through the seams as opaque
 * FGameplayTag values and is never hard-coded here.
 */
namespace InvUITags
{
	// --- Service-locator keys (published by genre modules / the registry itself) ---

	/** Service-locator key for the live UInvUI_ContainerRegistry (world-scoped, weak-observed). */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_ContainerRegistry);

	/** Service-locator key for the active IInvUI_ItemDisplay icon/text resolver. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_ItemDisplay);

	// --- Player -> server intent verbs (routed through the player-owned intent component) ---
	// The window UI never mutates a container directly; it emits one of these intents and the
	// server re-derives the real target/slot from identity. Anchored under DP.InvUI.Intent.

	/** Intent: move/transfer a stack from one identity-keyed slot to another. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Intent_Move);

	/** Intent: split a stack into two (count specified by the intent payload). */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Intent_Split);

	/** Intent: merge/stack one slot's contents onto a compatible target slot. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Intent_Merge);

	/** Intent: drop/eject a stack out of the container. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Intent_Drop);

	/** Intent: use/activate the item in a slot. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Intent_Use);

	/** Intent: equip the item in a slot into a paper-doll/equipment slot. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Intent_Equip);

	/** Intent: sort the whole container using the active sort strategy. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Intent_Sort);

	// --- Container capability advertisements (queried by the UI to enable affordances) ---
	// A backend/container exposes these so the window can show/hide drag, split, sort, etc.
	// without coupling to the backend type. Anchored under DP.InvUI.Cap.

	/** Capability: the container permits moving stacks between its slots / out of it. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cap_Move);

	/** Capability: the container permits splitting stacks. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cap_Split);

	/** Capability: the container supports server-side sorting. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cap_Sort);

	/** Capability: the container is a fixed-slot (named slot) container, e.g. equipment. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cap_FixedSlots);

	/** Capability: the container is read-only from the UI (e.g. a vendor preview). */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Cap_ReadOnly);

	// --- Canonical inventory screen ids (registered under the core DP.UI.Screen root) ---

	/** Screen id for the bag / backpack window. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Bag);

	/** Screen id for the equipment / paper-doll window. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Equipment);

	/** Screen id for a container/chest/storage window. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Container);

	/** Screen id for a vendor/shop window. */
	DESIGNPATTERNSINVENTORYUI_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Screen_Vendor);
}
