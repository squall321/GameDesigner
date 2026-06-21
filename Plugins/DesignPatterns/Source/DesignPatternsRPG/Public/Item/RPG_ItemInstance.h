// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Stats/RPG_StatsComponent.h"   // FRPG_StatModifier, ERPG_StatModOp
#include "Net/Seam_NetValue.h"          // FSeam_NetValue (net-safe rolled magnitudes)
#include "RPG_ItemInstance.generated.h"

/**
 * One rolled affix on an item instance.
 *
 * An affix targets a tag-keyed attribute with an operation and a net-safe rolled magnitude. The magnitude
 * rides an FSeam_NetValue (Float) so it can travel inside a replicated fast-array item and into a save
 * without ever being a raw FInstancedStruct. AffixDefTag references the URPG_AffixDefinition that produced
 * it, so tooling can re-resolve range/budget data.
 *
 * Maps 1:1 onto the existing FRPG_StatModifier via ToStatModifier(): the stats component never learns about
 * affixes, only modifiers, keeping the per-instance system decoupled from the stat folding rules.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_ItemAffix
{
	GENERATED_BODY()

	/** Identity tag of the URPG_AffixDefinition that rolled this affix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item|Affix")
	FGameplayTag AffixDefTag;

	/** Attribute this affix modifies (e.g. "RPG.Attribute.Strength"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item|Affix")
	FGameplayTag AttributeTag;

	/** Combine operation, mirroring ERPG_StatModOp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item|Affix")
	ERPG_StatModOp Op = ERPG_StatModOp::Additive;

	/** Rolled magnitude (Float). Net-safe; SaveGame-flagged. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item|Affix")
	FSeam_NetValue RolledMagnitude;

	FRPG_ItemAffix() = default;

	/** True if this affix has a target attribute. */
	bool IsValidAffix() const { return AttributeTag.IsValid(); }

	/** The rolled magnitude as a float (0 if unset / wrong type). */
	float GetMagnitude() const
	{
		return RolledMagnitude.Type == ESeam_NetValueType::Float
			? static_cast<float>(RolledMagnitude.FloatValue)
			: 0.f;
	}

	/** Convert to a stat modifier grouped under SourceTag (so a whole item's affixes remove together). */
	FRPG_StatModifier ToStatModifier(const FGameplayTag& SourceTag) const
	{
		FRPG_StatModifier Mod;
		Mod.AttributeTag = AttributeTag;
		Mod.Op = Op;
		Mod.Magnitude = GetMagnitude();
		Mod.SourceTag = SourceTag;
		return Mod;
	}

	bool operator==(const FRPG_ItemAffix& Other) const
	{
		return AffixDefTag == Other.AffixDefTag
			&& AttributeTag == Other.AttributeTag
			&& Op == Other.Op
			&& RolledMagnitude == Other.RolledMagnitude;
	}
};

/**
 * One gem/rune socket on an item instance: a socket-type tag plus the identity tag of the gem currently
 * socketed (empty = open socket).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_ItemSocket
{
	GENERATED_BODY()

	/** What kind of gem/rune this socket accepts (e.g. "RPG.Socket.Gem"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item|Socket")
	FGameplayTag SocketTypeTag;

	/** Identity tag of the socketed item, or empty when the socket is open. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item|Socket")
	FGameplayTag SocketedItemTag;

	FRPG_ItemSocket() = default;

	/** True when no gem occupies this socket. */
	bool IsOpen() const { return !SocketedItemTag.IsValid(); }

	bool operator==(const FRPG_ItemSocket& Other) const
	{
		return SocketTypeTag == Other.SocketTypeTag && SocketedItemTag == Other.SocketedItemTag;
	}
};

/**
 * Per-instance mutable state for a single non-stackable piece of gear.
 *
 * The shared, immutable definition (URPG_ItemDefinition) is referenced by ItemTag; everything that varies
 * between two otherwise-identical items lives here: a stable InstanceId (used to address the item across the
 * inventory/equipment fast-arrays and saves), rolled rarity, item/upgrade levels, rolled affixes, sockets
 * and durability. Every field is SaveGame-flagged so the whole instance persists through the existing save.
 *
 * This struct carries NO UObject pointers, so it is trivially copyable into a fast-array item, a save record
 * and across the wire (its FSeam_NetValue affix magnitudes net-serialize compactly).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_ItemInstance
{
	GENERATED_BODY()

	/** Definition identity (matches URPG_ItemDefinition::DataTag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item")
	FGameplayTag ItemTag;

	/** Stable per-instance id, unique within the owning instance component. 0 = invalid/unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item")
	int32 InstanceId = 0;

	/** Rolled rarity tier (e.g. "RPG.Rarity.Rare"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item")
	FGameplayTag RarityTag;

	/** Item level used to scale affix magnitude rolls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item")
	int32 ItemLevel = 1;

	/** Upgrade/enchant level applied on top of the base item (0 = unmodified). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item")
	int32 UpgradeLevel = 0;

	/** Rolled affixes contributing stat modifiers while equipped. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item")
	TArray<FRPG_ItemAffix> Affixes;

	/** Sockets (open or filled). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item")
	TArray<FRPG_ItemSocket> Sockets;

	/** Current durability (defensive local fallback when no ISeam_ItemDurability backend is present). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item", meta = (ClampMin = "0.0"))
	float CurrentDurability = 0.f;

	/** Maximum durability (defines the [0,1] normalization for the durability seam fallback). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "RPG|Item", meta = (ClampMin = "0.0"))
	float MaxDurability = 0.f;

	FRPG_ItemInstance() = default;

	/** True if this instance addresses a real item and has a valid id. */
	bool IsValidInstance() const { return ItemTag.IsValid() && InstanceId != 0; }

	/** Normalized durability in [0,1]; 1 when MaxDurability is 0 (treated as indestructible fallback). */
	float GetDurabilityNormalized() const
	{
		return MaxDurability > 0.f ? FMath::Clamp(CurrentDurability / MaxDurability, 0.f, 1.f) : 1.f;
	}

	bool operator==(const FRPG_ItemInstance& Other) const
	{
		return ItemTag == Other.ItemTag
			&& InstanceId == Other.InstanceId
			&& RarityTag == Other.RarityTag
			&& ItemLevel == Other.ItemLevel
			&& UpgradeLevel == Other.UpgradeLevel
			&& Affixes == Other.Affixes
			&& Sockets == Other.Sockets
			&& CurrentDurability == Other.CurrentDurability
			&& MaxDurability == Other.MaxDurability;
	}
	bool operator!=(const FRPG_ItemInstance& Other) const { return !(*this == Other); }
};
