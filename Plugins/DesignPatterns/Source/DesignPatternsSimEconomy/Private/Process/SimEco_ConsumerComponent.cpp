// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Process/SimEco_ConsumerComponent.h"
#include "Process/SimEco_ProcessDef.h"
#include "Commodity/SimEco_StockpileComponent.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"

void USimEco_ConsumerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimEco_ConsumerComponent, bStarved);
}

void USimEco_ConsumerComponent::GetExpectedInputs_Implementation(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const
{
	OutCommodities.Reset();
	OutQuantities.Reset();
	if (const USimEco_ProcessDef* Def = ResolveProcessDef())
	{
		for (const FSimEco_CommodityAmount& In : Def->Inputs)
		{
			OutCommodities.Add(In.Commodity);
			OutQuantities.Add(static_cast<float>(In.Quantity));
		}
	}
}

void USimEco_ConsumerComponent::SetStarved(bool bNewStarved)
{
	// AUTHORITY GUARD: replicated state.
	if (!HasAuthority())
	{
		return;
	}
	if (bStarved == bNewStarved)
	{
		return;
	}
	bStarved = bNewStarved;
	OnRep_Starved(); // server-side local notify
}

bool USimEco_ConsumerComponent::OnCycleBegun(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile)
{
	// SERVER: reserve all inputs; roll back partials on shortage and flag starvation.
	for (const FSimEco_CommodityAmount& In : Def.Inputs)
	{
		if (!In.IsValidAmount())
		{
			continue;
		}
		const double Reserved = Stockpile.Reserve(In.Commodity, In.Quantity);
		if (Reserved + KINDA_SMALL_NUMBER < In.Quantity)
		{
			Stockpile.ReleaseReserved(In.Commodity, Reserved);
			for (const FSimEco_CommodityAmount& Prior : Def.Inputs)
			{
				if (Prior.Commodity == In.Commodity)
				{
					break;
				}
				if (Prior.IsValidAmount())
				{
					Stockpile.ReleaseReserved(Prior.Commodity, Prior.Quantity);
				}
			}
			SetStarved(true);
			return false;
		}
	}
	SetStarved(false);
	return true;
}

void USimEco_ConsumerComponent::OnCycleCompleted(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile)
{
	// SERVER: consume the reserved inputs; deposit any outputs (a consumer recipe may still yield).
	for (const FSimEco_CommodityAmount& In : Def.Inputs)
	{
		if (In.IsValidAmount())
		{
			Stockpile.CommitReserved(In.Commodity, In.Quantity);
		}
	}
	for (const FSimEco_CommodityAmount& Out : Def.Outputs)
	{
		if (Out.IsValidAmount())
		{
			Stockpile.Add(Out.Commodity, Out.Quantity);
		}
	}
	UE_LOG(LogDP, Verbose, TEXT("[SimEco_Consumer] %s completed a consumption cycle of %s"),
		*GetNameSafe(GetOwner()), *GetProcessTag().ToString());
}

void USimEco_ConsumerComponent::OnProcessCancelled(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile)
{
	// SERVER: release reserved-but-uncommitted inputs and clear the starvation flag.
	if (bServerCycleReserved)
	{
		for (const FSimEco_CommodityAmount& In : Def.Inputs)
		{
			if (In.IsValidAmount())
			{
				Stockpile.ReleaseReserved(In.Commodity, In.Quantity);
			}
		}
	}
	SetStarved(false);
}

void USimEco_ConsumerComponent::OnRep_Starved()
{
	UE_LOG(LogDP, Verbose, TEXT("[SimEco_Consumer] %s starvation = %s"),
		*GetNameSafe(GetOwner()), bStarved ? TEXT("true") : TEXT("false"));
}
