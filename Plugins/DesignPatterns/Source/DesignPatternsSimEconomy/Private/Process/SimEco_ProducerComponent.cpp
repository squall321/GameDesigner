// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Process/SimEco_ProducerComponent.h"
#include "Process/SimEco_ProcessDef.h"
#include "Commodity/SimEco_StockpileComponent.h"
#include "Core/DPLog.h"

void USimEco_ProducerComponent::GetExpectedOutputs_Implementation(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const
{
	OutCommodities.Reset();
	OutQuantities.Reset();
	if (const USimEco_ProcessDef* Def = ResolveProcessDef())
	{
		for (const FSimEco_CommodityAmount& Out : Def->Outputs)
		{
			OutCommodities.Add(Out.Commodity);
			OutQuantities.Add(static_cast<float>(Out.Quantity));
		}
	}
}

bool USimEco_ProducerComponent::OnCycleBegun(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile)
{
	// SERVER (base guarantees authority): reserve every input. If any cannot be fully reserved,
	// roll back the partial reservations and stall this cycle.
	for (const FSimEco_CommodityAmount& In : Def.Inputs)
	{
		if (!In.IsValidAmount())
		{
			continue;
		}
		const double Reserved = Stockpile.Reserve(In.Commodity, In.Quantity);
		if (Reserved + KINDA_SMALL_NUMBER < In.Quantity)
		{
			// Shortage: release whatever we already reserved this cycle and signal a stall.
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
			UE_LOG(LogDP, Verbose, TEXT("[SimEco_Producer] %s stalled: short on %s"),
				*GetNameSafe(GetOwner()), *In.Commodity.ToString());
			return false;
		}
	}
	return true;
}

void USimEco_ProducerComponent::OnCycleCompleted(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile)
{
	// SERVER: commit the reserved inputs (consume them) then deposit the outputs.
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
	UE_LOG(LogDP, Verbose, TEXT("[SimEco_Producer] %s completed a cycle of %s"),
		*GetNameSafe(GetOwner()), *GetProcessTag().ToString());
}

void USimEco_ProducerComponent::OnProcessCancelled(const USimEco_ProcessDef& Def, USimEco_StockpileComponent& Stockpile)
{
	// SERVER: hand back any reserved-but-uncommitted inputs from the in-flight cycle.
	if (!bServerCycleReserved)
	{
		return;
	}
	for (const FSimEco_CommodityAmount& In : Def.Inputs)
	{
		if (In.IsValidAmount())
		{
			Stockpile.ReleaseReserved(In.Commodity, In.Quantity);
		}
	}
}
