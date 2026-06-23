// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Window/InvUI_TransferWindow.h"
#include "Widget/InvUI_GridWidget.h"
#include "Mediator/InvUI_ContainerMediatorComponent.h"
#include "Registry/InvUI_ContainerRegistry.h"
#include "Core/DPLog.h"

FInvUI_ContainerInstanceId UInvUI_TransferWindow::GetContainerIdForRole(const FGameplayTag& RoleTag) const
{
	// The role's hosted grid carries the bound container id publicly (GetContainerId reads it off the
	// grid VM). This avoids needing the base's private role->id mapping.
	if (const TObjectPtr<UInvUI_GridWidget>* GridPtr = HostedGrids.Find(RoleTag))
	{
		if (*GridPtr)
		{
			return (*GridPtr)->GetContainerId();
		}
	}
	return FInvUI_ContainerInstanceId::Invalid();
}

void UInvUI_TransferWindow::TakeAll()
{
	UInvUI_ContainerMediatorComponent* Med = GetMediator();
	if (Med == nullptr)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] TransferWindow '%s': no mediator; TakeAll ignored."), *GetName());
		return;
	}

	const FInvUI_ContainerInstanceId SourceId = GetContainerIdForRole(SourceRole);
	const FInvUI_ContainerInstanceId DestId = GetContainerIdForRole(DestRole);
	if (!SourceId.IsValid() || !DestId.IsValid())
	{
		UE_LOG(LogDP, Verbose, TEXT("[InvUI] TransferWindow '%s': source/dest unbound; TakeAll ignored."), *GetName());
		return;
	}

	UInvUI_ContainerRegistry* Registry = UInvUI_ContainerRegistry::Get(this);
	if (Registry == nullptr)
	{
		return;
	}
	TScriptInterface<IInvUI_ItemContainer> Source = Registry->ResolveContainer(SourceId);
	if (Source.GetObject() == nullptr)
	{
		return;
	}

	// Snapshot the source slots and request one whole-stack move per occupied slot. Each move is
	// re-resolved and re-validated authoritatively by the server; ordering is the snapshot order.
	TArray<FInvUI_SlotState> Slots;
	IInvUI_ItemContainer::Execute_GetSlots(Source.GetObject(), Slots);
	for (const FInvUI_SlotState& Slot : Slots)
	{
		if (Slot.IsOccupied())
		{
			Med->RequestMoveByIdentity(SourceId, Slot.SlotTag, DestId, FGameplayTag(), 0);
		}
	}
}

void UInvUI_TransferWindow::TransferSlot(FGameplayTag SourceSlotTag, int32 Count)
{
	UInvUI_ContainerMediatorComponent* Med = GetMediator();
	if (Med == nullptr || !SourceSlotTag.IsValid())
	{
		return;
	}
	const FInvUI_ContainerInstanceId SourceId = GetContainerIdForRole(SourceRole);
	const FInvUI_ContainerInstanceId DestId = GetContainerIdForRole(DestRole);
	if (!SourceId.IsValid() || !DestId.IsValid())
	{
		return;
	}
	Med->RequestMoveByIdentity(SourceId, SourceSlotTag, DestId, FGameplayTag(), FMath::Max(0, Count));
}

void UInvUI_TransferWindow::RefreshCurrencyRow(const TScriptInterface<ISeam_Wallet>& Wallet)
{
	TArray<FInvUI_CurrencyRowEntry> Rows;

	if (UObject* WalletObj = Wallet.GetObject())
	{
		TMap<FGameplayTag, int64> Balances;
		ISeam_Wallet::Execute_GetAllBalances(WalletObj, Balances);
		Rows.Reserve(Balances.Num());
		for (const TPair<FGameplayTag, int64>& Pair : Balances)
		{
			Rows.Emplace(Pair.Key, Pair.Value);
		}
	}

	OnCurrencyRow(Rows);
}
