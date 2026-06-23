// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Window/InvUI_WindowBase.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Economy/Seam_Wallet.h"
#include "InvUI_TransferWindow.generated.h"

/** One currency row in the transfer window's purse readout (tag-keyed balance). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_CurrencyRowEntry
{
	GENERATED_BODY()

	/** Which currency (soft/hard/event tag). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Transfer")
	FGameplayTag CurrencyTag;

	/** The balance in that currency. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Transfer")
	int64 Balance = 0;

	FInvUI_CurrencyRowEntry() = default;
	FInvUI_CurrencyRowEntry(const FGameplayTag& InTag, int64 InBalance)
		: CurrencyTag(InTag), Balance(InBalance) {}
};

/**
 * Two-container loot/transfer WINDOW (loot a chest/corpse into the bag, move between two bags).
 *
 * Hosts a SOURCE grid and a DEST grid under two role tags (reusing the base RegisterHostedGrid /
 * BindContainers). TakeAll iterates the source container's slots and issues one mediator
 * RequestMoveByIdentity per occupied slot with an empty ToSlot (the server re-validates each move).
 * An optional currency row reads a looted actor's ISeam_Wallet::GetAllBalances (read-only) and pushes
 * it to the BP. All transfers flow through the EXISTING mediator — the window adds NO new RPC.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Transfer Window"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_TransferWindow : public UInvUI_WindowBase
{
	GENERATED_BODY()

public:
	/** Role of the source (looted) grid within this window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Transfer")
	FGameplayTag SourceRole;

	/** Role of the destination (player) grid within this window. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "InvUI|Transfer")
	FGameplayTag DestRole;

	/**
	 * Move every occupied source slot to the destination container (empty ToSlot, whole stack each).
	 * Each move is independently re-validated on the server. No-op if either role is unbound.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Transfer")
	void TakeAll();

	/**
	 * Move a single source slot's stack (Count units; 0 = whole stack) to the destination container.
	 * Routed through the mediator.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Transfer")
	void TransferSlot(FGameplayTag SourceSlotTag, int32 Count);

	/**
	 * Read Wallet's balances (via the read-only seam) and push them to the BP as currency rows. Pass
	 * the looted actor's wallet seam (resolved by the caller off the looted actor). No-op if empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Transfer")
	void RefreshCurrencyRow(const TScriptInterface<ISeam_Wallet>& Wallet);

protected:
	/** Designer hook fired with the resolved currency rows so the BP paints the purse readout. */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|Transfer", meta = (DisplayName = "On Currency Row"))
	void OnCurrencyRow(const TArray<FInvUI_CurrencyRowEntry>& Rows);

private:
	/** Resolve the bound container id for a role from this window's bound set, or invalid. */
	FInvUI_ContainerInstanceId GetContainerIdForRole(const FGameplayTag& RoleTag) const;
};
