// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "RPG_ItemCraftComponent.generated.h"

struct FRPG_ItemInstance;
class URPG_CraftCostTable;
class URPG_ItemRoller;
class URPG_ItemInstanceComponent;
class URPG_InventoryComponent;
class ISeam_ItemDurability;
class ISeam_Wallet;

/**
 * Player-owned component exposing authority-only item operations on rolled instances: upgrade/enchant,
 * affix reroll, salvage and repair.
 *
 * Every operation is requested from the owning client via a Server...WithValidation RPC; the server
 * re-derives all costs/eligibility (never trusting the client) and mutates the actor's
 * URPG_ItemInstanceComponent under authority. Material costs and salvage yields come from a data-driven
 * URPG_CraftCostTable; rerolls draw a fresh deterministic seed server-side and re-roll a single affix
 * through URPG_ItemRoller's affix machinery. Durability repair prefers an ISeam_ItemDurability backend
 * (e.g. a survival durability component) and falls back to the instance's CurrentDurability field when none
 * is present. Currency affordability is checked read-only through ISeam_Wallet (the debit stays on the
 * guarded wallet, not here).
 *
 * Because this component issues client->server intent, it must live on a player-(connection-)owned actor
 * (PlayerState / Pawn / PlayerController-owned), and it replicates so the RPCs route correctly.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_ItemCraftComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URPG_ItemCraftComponent();

	/** Client->server request to upgrade the instance one level. */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RPG|Craft")
	void ServerUpgradeInstance(int32 InstanceId);

	/** Client->server request to reroll one affix (by index) on the instance. */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RPG|Craft")
	void ServerRerollAffix(int32 InstanceId, int32 AffixIndex);

	/** Client->server request to salvage the instance into materials and destroy it. */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RPG|Craft")
	void ServerSalvageInstance(int32 InstanceId);

	/** Client->server request to repair the instance by Amount (normalized units). */
	UFUNCTION(Server, Reliable, WithValidation, BlueprintCallable, Category = "RPG|Craft")
	void ServerRepairInstance(int32 InstanceId, float Amount);

	/** Cost/yield economics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Craft")
	TObjectPtr<URPG_CraftCostTable> CostTable;

	/** Roller used for deterministic affix rerolls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Craft")
	TObjectPtr<URPG_ItemRoller> Roller;

protected:
	/** Apply one upgrade level to Inst in place (authority context). Returns true if applied. */
	bool ApplyUpgrade_Authority(FRPG_ItemInstance& Inst);

	/** Resolve the per-instance carrier off the owning actor. May be null. */
	URPG_ItemInstanceComponent* ResolveInstanceComponent() const;

	/** Resolve the stackable inventory (salvage materials/cost target). May be null. */
	URPG_InventoryComponent* ResolveInventory() const;

	/** Resolve an optional durability backend tracking InstanceId; empty when none. */
	TScriptInterface<ISeam_ItemDurability> ResolveDurabilitySeam(int32 InstanceId) const;

	/** Resolve the owner's read-only wallet seam; empty when none. */
	TScriptInterface<ISeam_Wallet> ResolveWallet() const;

	/** True if the owner can pay Materials (and any currency) for an op. */
	bool CanAffordCost(const struct FRPG_CraftCostRow& Cost) const;

	/** Consume Materials from the inventory (authority). Assumes CanAffordCost passed. */
	void ConsumeMaterials(const TArray<struct FRPG_ItemStack>& Materials);
};
