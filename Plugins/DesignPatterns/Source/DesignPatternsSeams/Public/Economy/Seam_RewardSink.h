// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Net/Seam_NetValue.h"
#include "Seam_RewardSink.generated.h"

/**
 * One line-item of a reward: either a currency credit or an item grant, with a magnitude.
 *
 * A reward is a small, value-typed bundle so a quest / achievement / encounter can describe "what the
 * player gets" without depending on the wallet or inventory module. The economy's reward sink resolves
 * the actual currency wallet (via ISeam_WalletAuthority) and item inventory (via ISeam_PurchaseTarget)
 * off the receiving actor and pays the line out under authority.
 *
 * The magnitude rides in an FSeam_NetValue so the spec rule "no raw arbitrary value crosses the wire"
 * holds even when a reward spec is replicated as part of a larger payload.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_RewardLine
{
	GENERATED_BODY()

	/**
	 * True if this line pays currency (CurrencyOrItemTag is a currency tag); false if it grants an item
	 * (CurrencyOrItemTag is an item tag).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Economy")
	bool bIsCurrency = true;

	/** The currency tag (when bIsCurrency) or the item tag (when !bIsCurrency) this line pays. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Economy")
	FGameplayTag CurrencyOrItemTag;

	/**
	 * Whole-unit amount to pay. Currency lines credit this many units; item lines grant this many.
	 * Carried as an int magnitude (FSeam_NetValue::MakeInt) so the value is net-safe inside a payload.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Economy")
	FSeam_NetValue Amount = FSeam_NetValue::MakeInt(0);

	FSeam_RewardLine() = default;

	/** Build a currency line. */
	static FSeam_RewardLine Currency(const FGameplayTag& CurrencyTag, int64 InAmount)
	{
		FSeam_RewardLine L;
		L.bIsCurrency = true;
		L.CurrencyOrItemTag = CurrencyTag;
		L.Amount = FSeam_NetValue::MakeInt(InAmount);
		return L;
	}

	/** Build an item line. */
	static FSeam_RewardLine Item(const FGameplayTag& ItemTag, int32 Count)
	{
		FSeam_RewardLine L;
		L.bIsCurrency = false;
		L.CurrencyOrItemTag = ItemTag;
		L.Amount = FSeam_NetValue::MakeInt((int64)Count);
		return L;
	}

	/** Whole-unit amount as int64 (reads the FSeam_NetValue defensively). */
	int64 GetAmount() const
	{
		return (Amount.Type == ESeam_NetValueType::Int) ? Amount.IntValue
			: (Amount.Type == ESeam_NetValueType::Float) ? (int64)Amount.FloatValue
			: 0;
	}

	bool IsValidLine() const { return CurrencyOrItemTag.IsValid() && GetAmount() > 0; }
};

/**
 * A complete reward to pay out: a source tag (who/what awarded it, for logging/anti-replay) plus the
 * list of currency/item lines. Plain value type; cheap to copy across a seam call.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_RewardSpec
{
	GENERATED_BODY()

	/** Identity of what granted the reward (quest tag, achievement tag, encounter tag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Economy")
	FGameplayTag SourceTag;

	/** The currency/item lines to pay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Seam|Economy")
	TArray<FSeam_RewardLine> Lines;

	bool IsValidSpec() const { return SourceTag.IsValid() && Lines.Num() > 0; }
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_RewardSink : public UInterface
{
	GENERATED_BODY()
};

/**
 * AUTHORITY-ONLY seam for paying a reward out to a player. Replaces the "bus-as-command" anti-pattern:
 * a quest / achievement system does NOT broadcast a "give the player gold" command on the message bus;
 * instead it resolves the reward sink (registered under DP.Service.Eco.RewardSink, or off the actor)
 * and calls PayReward, which atomically credits currency (ISeam_WalletAuthority::Grant) and grants
 * items (ISeam_PurchaseTarget::GrantItem) on the receiving actor under authority.
 *
 * PayReward is AUTHORITY ONLY — implementations guard HasAuthority() at the TOP and return false on a
 * client. The bus is used only AFTER the fact, to notify UI that a reward landed.
 */
class DESIGNPATTERNSSEAMS_API ISeam_RewardSink
{
	GENERATED_BODY()

public:
	/**
	 * Pay Spec out to Receiver. AUTHORITY ONLY. Returns true if every valid line was paid (currency
	 * credited and/or items granted). A partial failure (e.g. inventory full) returns false; the
	 * implementation documents whether it rolls back or pays best-effort.
	 *
	 * @param Receiver  The actor to pay (must expose ISeam_WalletAuthority for currency lines and
	 *                  ISeam_PurchaseTarget for item lines, directly or via a component).
	 * @param Spec      The reward to pay.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Economy")
	bool PayReward(AActor* Receiver, const FSeam_RewardSpec& Spec);
};
