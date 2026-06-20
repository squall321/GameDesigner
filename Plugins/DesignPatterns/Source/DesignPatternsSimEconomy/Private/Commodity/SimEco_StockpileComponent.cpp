// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Commodity/SimEco_StockpileComponent.h"
#include "Commodity/SimEco_CommodityDef.h"
#include "Settings/SimEco_DeveloperSettings.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

//~ FSimEco_StockEntry replication callbacks (client side) ------------------------------------

void FSimEco_StockEntry::PreReplicatedRemove(const FSimEco_StockArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimEco_StockEntry::PostReplicatedAdd(const FSimEco_StockArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimEco_StockEntry::PostReplicatedChange(const FSimEco_StockArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

//~ USimEco_StockpileComponent ----------------------------------------------------------------

USimEco_StockpileComponent::USimEco_StockpileComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	bWantsInitializeComponent = true;

	// Wire the fast-array back-pointer so entry callbacks can notify us (server and client).
	Stock.OwnerComponent = this;
}

void USimEco_StockpileComponent::InitializeComponent()
{
	Super::InitializeComponent();
	// Re-assert the back-pointer in case the struct was default-constructed during load.
	Stock.OwnerComponent = this;
}

void USimEco_StockpileComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimEco_StockpileComponent, Stock);
}

bool USimEco_StockpileComponent::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

double USimEco_StockpileComponent::GetQuantityEpsilon() const
{
	if (const USimEco_DeveloperSettings* Settings = USimEco_DeveloperSettings::Get())
	{
		return FMath::Max(0.0, Settings->QuantityEpsilon);
	}
	return 1.0e-4;
}

double USimEco_StockpileComponent::QuantizeForCommodity(const FGameplayTag& Commodity, double RawQuantity) const
{
	if (RawQuantity <= 0.0)
	{
		return 0.0;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		if (const USimEco_CommodityDef* Def = Registry->Find<USimEco_CommodityDef>(Commodity))
		{
			return Def->Quantize(RawQuantity);
		}
	}
	// Unknown commodity: treat as divisible (no quantization).
	return RawQuantity;
}

int32 USimEco_StockpileComponent::FindEntryIndex(const FGameplayTag& Commodity) const
{
	for (int32 Index = 0; Index < Stock.Entries.Num(); ++Index)
	{
		if (Stock.Entries[Index].Commodity == Commodity)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

FSimEco_StockEntry& USimEco_StockpileComponent::FindOrAddEntry(const FGameplayTag& Commodity)
{
	const int32 Existing = FindEntryIndex(Commodity);
	if (Existing != INDEX_NONE)
	{
		return Stock.Entries[Existing];
	}
	FSimEco_StockEntry& NewEntry = Stock.Entries.AddDefaulted_GetRef();
	NewEntry.Commodity = Commodity;
	return NewEntry;
}

bool USimEco_StockpileComponent::PruneIfEmpty(int32 EntryIndex)
{
	if (!Stock.Entries.IsValidIndex(EntryIndex))
	{
		return false;
	}
	const double Eps = GetQuantityEpsilon();
	const FSimEco_StockEntry& Entry = Stock.Entries[EntryIndex];
	if (Entry.Quantity <= Eps && Entry.Reserved <= Eps)
	{
		Stock.Entries.RemoveAt(EntryIndex);
		Stock.MarkArrayDirty();
		return true;
	}
	return false;
}

double USimEco_StockpileComponent::Add(FGameplayTag Commodity, double Quantity)
{
	// AUTHORITY GUARD: never mutate replicated stock on a client.
	if (!HasAuthority())
	{
		return 0.0;
	}
	if (!Commodity.IsValid())
	{
		return 0.0;
	}

	const double ToAdd = QuantizeForCommodity(Commodity, Quantity);
	if (ToAdd <= GetQuantityEpsilon())
	{
		return 0.0;
	}

	FSimEco_StockEntry& Entry = FindOrAddEntry(Commodity);
	Entry.Quantity += ToAdd;
	Stock.MarkItemDirty(Entry);

	NotifyStockChanged();
	UE_LOG(LogDP, Verbose, TEXT("[SimEco_Stockpile] Added %.4f x %s"), ToAdd, *Commodity.ToString());
	return ToAdd;
}

double USimEco_StockpileComponent::Remove(FGameplayTag Commodity, double Quantity)
{
	// AUTHORITY GUARD.
	if (!HasAuthority())
	{
		return 0.0;
	}
	if (!Commodity.IsValid() || Quantity <= 0.0)
	{
		return 0.0;
	}

	const int32 Index = FindEntryIndex(Commodity);
	if (Index == INDEX_NONE)
	{
		return 0.0;
	}

	FSimEco_StockEntry& Entry = Stock.Entries[Index];
	const double Eps = GetQuantityEpsilon();

	// Only free (unreserved) units may be removed; reserved units are protected.
	const double Free = Entry.GetAvailable();
	double ToRemove = FMath::Min(Free, Quantity);
	if (ToRemove <= Eps)
	{
		return 0.0;
	}

	Entry.Quantity -= ToRemove;
	if (Entry.Quantity <= Eps)
	{
		// Clamp residue; keep Reserved consistent (it cannot exceed Quantity).
		Entry.Quantity = FMath::Max(Entry.Quantity, Entry.Reserved);
	}

	if (!PruneIfEmpty(Index))
	{
		Stock.MarkItemDirty(Entry);
	}

	NotifyStockChanged();
	UE_LOG(LogDP, Verbose, TEXT("[SimEco_Stockpile] Removed %.4f x %s"), ToRemove, *Commodity.ToString());
	return ToRemove;
}

double USimEco_StockpileComponent::Reserve(FGameplayTag Commodity, double Quantity)
{
	// AUTHORITY GUARD.
	if (!HasAuthority())
	{
		return 0.0;
	}
	if (!Commodity.IsValid() || Quantity <= 0.0)
	{
		return 0.0;
	}

	const int32 Index = FindEntryIndex(Commodity);
	if (Index == INDEX_NONE)
	{
		return 0.0;
	}

	FSimEco_StockEntry& Entry = Stock.Entries[Index];
	const double Eps = GetQuantityEpsilon();

	const double Free = Entry.GetAvailable();
	const double ToReserve = FMath::Min(Free, Quantity);
	if (ToReserve <= Eps)
	{
		return 0.0;
	}

	Entry.Reserved = FMath::Min(Entry.Quantity, Entry.Reserved + ToReserve);
	Stock.MarkItemDirty(Entry);

	NotifyStockChanged();
	UE_LOG(LogDP, Verbose, TEXT("[SimEco_Stockpile] Reserved %.4f x %s"), ToReserve, *Commodity.ToString());
	return ToReserve;
}

double USimEco_StockpileComponent::CommitReserved(FGameplayTag Commodity, double Quantity)
{
	// AUTHORITY GUARD.
	if (!HasAuthority())
	{
		return 0.0;
	}
	if (!Commodity.IsValid() || Quantity <= 0.0)
	{
		return 0.0;
	}

	const int32 Index = FindEntryIndex(Commodity);
	if (Index == INDEX_NONE)
	{
		return 0.0;
	}

	FSimEco_StockEntry& Entry = Stock.Entries[Index];
	const double Eps = GetQuantityEpsilon();

	// Can only commit what is actually reserved.
	const double ToCommit = FMath::Min(Entry.Reserved, Quantity);
	if (ToCommit <= Eps)
	{
		return 0.0;
	}

	Entry.Reserved -= ToCommit;
	Entry.Quantity -= ToCommit;

	// Clamp residue, then re-establish the invariant 0 <= Reserved <= Quantity. Clamping each field
	// independently can leave Reserved > Quantity from floating-point residue, so enforce it explicitly
	// (mirrors how Remove() clamps Quantity up to Reserved to preserve the same invariant).
	if (Entry.Reserved <= Eps) { Entry.Reserved = 0.0; }
	if (Entry.Quantity <= Eps) { Entry.Quantity = 0.0; }
	Entry.Reserved = FMath::Min(Entry.Reserved, Entry.Quantity);

	if (!PruneIfEmpty(Index))
	{
		Stock.MarkItemDirty(Entry);
	}

	NotifyStockChanged();
	UE_LOG(LogDP, Verbose, TEXT("[SimEco_Stockpile] Committed %.4f x %s"), ToCommit, *Commodity.ToString());
	return ToCommit;
}

double USimEco_StockpileComponent::ReleaseReserved(FGameplayTag Commodity, double Quantity)
{
	// AUTHORITY GUARD.
	if (!HasAuthority())
	{
		return 0.0;
	}
	if (!Commodity.IsValid() || Quantity <= 0.0)
	{
		return 0.0;
	}

	const int32 Index = FindEntryIndex(Commodity);
	if (Index == INDEX_NONE)
	{
		return 0.0;
	}

	FSimEco_StockEntry& Entry = Stock.Entries[Index];
	const double Eps = GetQuantityEpsilon();

	const double ToRelease = FMath::Min(Entry.Reserved, Quantity);
	if (ToRelease <= Eps)
	{
		return 0.0;
	}

	Entry.Reserved -= ToRelease;
	if (Entry.Reserved <= Eps) { Entry.Reserved = 0.0; }

	Stock.MarkItemDirty(Entry);

	NotifyStockChanged();
	UE_LOG(LogDP, Verbose, TEXT("[SimEco_Stockpile] Released %.4f x %s"), ToRelease, *Commodity.ToString());
	return ToRelease;
}

double USimEco_StockpileComponent::GetQuantity(FGameplayTag Commodity) const
{
	const int32 Index = FindEntryIndex(Commodity);
	return Index != INDEX_NONE ? Stock.Entries[Index].Quantity : 0.0;
}

double USimEco_StockpileComponent::GetReserved(FGameplayTag Commodity) const
{
	const int32 Index = FindEntryIndex(Commodity);
	return Index != INDEX_NONE ? Stock.Entries[Index].Reserved : 0.0;
}

double USimEco_StockpileComponent::GetAvailable(FGameplayTag Commodity) const
{
	const int32 Index = FindEntryIndex(Commodity);
	return Index != INDEX_NONE ? Stock.Entries[Index].GetAvailable() : 0.0;
}

void USimEco_StockpileComponent::HandleReplicatedChange()
{
	// Reached on clients from the fast-array entry callbacks: surface the change.
	NotifyStockChanged();
}

void USimEco_StockpileComponent::NotifyStockChanged_Implementation()
{
	OnStockChanged.Broadcast(this);
}
