// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Prog_ShopDefinition.generated.h"

class UProg_Condition;

/**
 * One purchasable line in a shop's catalogue.
 *
 * Authored entirely as data: which item is sold (by stable item tag delivered through the buyer's
 * ISeam_PurchaseTarget), which currency it costs and how much (the wallet currency tag read through
 * the buyer's ISeam_Wallet), the starting stock, and an optional unlock gate. Nothing here references
 * a concrete inventory/wallet/condition implementation by hard type — the item and currency are tags,
 * and the unlock is an instanced UProg_Condition (a sibling Progression area) referenced only by base
 * pointer, so the shop never hard-depends on a specific condition kind.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPROGRESSION_API FProg_ShopEntry
{
	GENERATED_BODY()

	/** Identity tag of the item granted on purchase (passed to ISeam_PurchaseTarget::GrantItem). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression|Shop")
	FGameplayTag ItemTag;

	/** Wallet currency tag this entry is priced in (matched against ISeam_Wallet balances). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression|Shop")
	FGameplayTag PriceCurrency;

	/** Unit price in PriceCurrency. Clamped to >= 0 at validation; 0 means free. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression|Shop", meta = (ClampMin = "0"))
	int64 Price = 0;

	/**
	 * Quantity of the item delivered per single purchase (a "bundle" size). One unit by default.
	 * Distinct from stock: Stock counts how many TIMES this entry can be bought.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression|Shop", meta = (ClampMin = "1"))
	int32 GrantCount = 1;

	/**
	 * How many times this entry may be purchased before it sells out. A value of -1 means
	 * infinite stock (never decrements, never sells out). Authored as the INITIAL stock; the
	 * live remaining stock lives on the replicated shop component, not in this data asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression|Shop", meta = (ClampMin = "-1"))
	int32 Stock = -1;

	/**
	 * Optional gate that must be satisfied (per-buyer) before this entry can be purchased or even
	 * shown as available. Instanced so each entry owns its own condition graph. Null means "always
	 * unlocked". Referenced only through the UProg_Condition base type (a sibling Progression area),
	 * never a concrete condition class, so the shop module stays decoupled.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Progression|Shop")
	TObjectPtr<UProg_Condition> UnlockCondition = nullptr;

	FProg_ShopEntry() = default;

	/** True if this entry is structurally usable (valid item tag and a non-negative bundle size). */
	bool IsValidEntry() const { return ItemTag.IsValid() && GrantCount >= 1; }

	/** True if this entry has a finite (depletable) stock count. */
	bool HasFiniteStock() const { return Stock >= 0; }
};

/**
 * A vendor's catalogue, authored as a tag-identified data asset.
 *
 * A UProg_ShopComponent on a vendor actor references one of these and exposes its entries to buyers.
 * The definition is immutable design-time data (entries, prices, unlock gates, initial stock); all
 * mutable, per-instance state (remaining stock) is held authoritatively on the component and
 * replicated, never written back into this asset. Resolve a shop by DataTag through the core data
 * registry just like any other UDP_DataAsset.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSPROGRESSION_API UProg_ShopDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * Optional human-facing title shown in the shop window header. Falls back to the base
	 * UDP_DataAsset::DisplayName when empty.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression|Shop")
	FText ShopTitle;

	/** The purchasable lines, in display order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Progression|Shop")
	TArray<FProg_ShopEntry> Entries;

	/** Number of authored entries (read-only convenience). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Progression|Shop")
	int32 NumEntries() const { return Entries.Num(); }

	/** True if EntryIndex addresses a valid entry. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Progression|Shop")
	bool IsValidEntryIndex(int32 EntryIndex) const { return Entries.IsValidIndex(EntryIndex); }

	/** The title to show (ShopTitle if set, otherwise the base DisplayName). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Progression|Shop")
	FText GetEffectiveTitle() const { return ShopTitle.IsEmpty() ? DisplayName : ShopTitle; }

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Validates entries: clamps prices, warns on empty item/currency tags and on duplicate items. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
