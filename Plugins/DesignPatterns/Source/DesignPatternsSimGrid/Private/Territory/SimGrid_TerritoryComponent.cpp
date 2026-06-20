// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Territory/SimGrid_TerritoryComponent.h"
#include "SimGrid_NativeTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

//~ FSimGrid_CellOwnershipEntry replication callbacks (client side) -----------------------------

void FSimGrid_CellOwnershipEntry::PreReplicatedRemove(const FSimGrid_OwnershipArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimGrid_CellOwnershipEntry::PostReplicatedAdd(const FSimGrid_OwnershipArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimGrid_CellOwnershipEntry::PostReplicatedChange(const FSimGrid_OwnershipArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

//~ USimGrid_TerritoryComponent ----------------------------------------------------------------

USimGrid_TerritoryComponent::USimGrid_TerritoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// Wire the fast-array back-pointer so entry callbacks can notify us (server and client).
	Ownership.OwnerComponent = this;
}

void USimGrid_TerritoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimGrid_TerritoryComponent, Ownership);
}

void USimGrid_TerritoryComponent::BeginPlay()
{
	Super::BeginPlay();

	// Build the read-acceleration index from whatever is already present (defaults / loaded state).
	RebuildIndex();

	// Publish ourselves so rules/systems can resolve the ownership carrier by service tag.
	PublishCarrierService(true);
}

void USimGrid_TerritoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	PublishCarrierService(false);
	Super::EndPlay(EndPlayReason);
}

bool USimGrid_TerritoryComponent::HasOwnerAuthority() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor && OwnerActor->HasAuthority();
}

void USimGrid_TerritoryComponent::PublishCarrierService(bool bRegister)
{
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}

	if (bRegister)
	{
		// WeakObserved: the carrier is owned by its actor, never by the locator (so it can't leak a
		// dead world's object). Allow override so a re-created carrier replaces a stale binding.
		Locator->RegisterService(SimGridTags::Service_TerritoryCarrier, this,
			EDP_ServiceLifetime::WeakObserved, /*bAllowOverride=*/true);
	}
	else
	{
		// Only unregister if we are still the bound provider (avoid clobbering a newer carrier).
		if (Locator->ResolveService(SimGridTags::Service_TerritoryCarrier) == this)
		{
			Locator->UnregisterService(SimGridTags::Service_TerritoryCarrier);
		}
	}
}

void USimGrid_TerritoryComponent::RebuildIndex()
{
	CellToEntryIndex.Reset();
	CellToEntryIndex.Reserve(Ownership.Entries.Num());
	for (int32 Index = 0; Index < Ownership.Entries.Num(); ++Index)
	{
		CellToEntryIndex.Add(Ownership.Entries[Index].Cell, Index);
	}
}

int32 USimGrid_TerritoryComponent::FindEntryIndex(const FSeam_CellCoord& Cell) const
{
	if (const int32* Found = CellToEntryIndex.Find(Cell))
	{
		// Defensive: the index can drift only if mutated outside this class; validate before trusting.
		if (Ownership.Entries.IsValidIndex(*Found) && Ownership.Entries[*Found].Cell == Cell)
		{
			return *Found;
		}
	}
	return INDEX_NONE;
}

int32 USimGrid_TerritoryComponent::ClaimCells(const TArray<FSeam_CellCoord>& Cells, FGameplayTag OwnerId)
{
	// AUTHORITY GUARD at the top: never mutate replicated ownership on a client.
	if (!HasOwnerAuthority())
	{
		return 0;
	}
	if (!OwnerId.IsValid() || Cells.Num() == 0)
	{
		return 0;
	}

	int32 Changed = 0;
	for (const FSeam_CellCoord& Cell : Cells)
	{
		const int32 ExistingIndex = FindEntryIndex(Cell);
		if (ExistingIndex != INDEX_NONE)
		{
			FSimGrid_CellOwnershipEntry& Entry = Ownership.Entries[ExistingIndex];
			if (Entry.OwnerId != OwnerId)
			{
				Entry.OwnerId = OwnerId;
				Ownership.MarkItemDirty(Entry);
				++Changed;
			}
		}
		else
		{
			const int32 NewIndex = Ownership.Entries.Add(FSimGrid_CellOwnershipEntry(Cell, OwnerId));
			CellToEntryIndex.Add(Cell, NewIndex);
			Ownership.MarkItemDirty(Ownership.Entries[NewIndex]);
			++Changed;
		}
	}

	if (Changed > 0)
	{
		NotifyTerritoryChanged();
		UE_LOG(LogDP, Verbose, TEXT("[SimGrid_Territory] Claimed %d cell(s) for %s."), Changed, *OwnerId.ToString());
	}
	return Changed;
}

int32 USimGrid_TerritoryComponent::ReleaseCells(const TArray<FSeam_CellCoord>& Cells, FGameplayTag bOnlyIfOwnedBy)
{
	// AUTHORITY GUARD.
	if (!HasOwnerAuthority())
	{
		return 0;
	}
	if (Cells.Num() == 0)
	{
		return 0;
	}

	int32 Released = 0;
	bool bIndexNeedsRebuild = false;

	for (const FSeam_CellCoord& Cell : Cells)
	{
		const int32 ExistingIndex = FindEntryIndex(Cell);
		if (ExistingIndex == INDEX_NONE)
		{
			continue;
		}
		if (bOnlyIfOwnedBy.IsValid() && Ownership.Entries[ExistingIndex].OwnerId != bOnlyIfOwnedBy)
		{
			// A caller may only release cells they own when a filter is supplied.
			continue;
		}

		Ownership.Entries.RemoveAt(ExistingIndex);
		Ownership.MarkArrayDirty();
		// RemoveAt shifts indices; rebuild the lookup once after the loop rather than per-removal.
		bIndexNeedsRebuild = true;
		++Released;
	}

	if (bIndexNeedsRebuild)
	{
		RebuildIndex();
	}
	if (Released > 0)
	{
		NotifyTerritoryChanged();
		UE_LOG(LogDP, Verbose, TEXT("[SimGrid_Territory] Released %d cell(s)."), Released);
	}
	return Released;
}

bool USimGrid_TerritoryComponent::ClaimCell(const FSeam_CellCoord& Cell, FGameplayTag OwnerId)
{
	TArray<FSeam_CellCoord> One;
	One.Add(Cell);
	return ClaimCells(One, OwnerId) > 0;
}

bool USimGrid_TerritoryComponent::ReleaseCell(const FSeam_CellCoord& Cell, FGameplayTag bOnlyIfOwnedBy)
{
	TArray<FSeam_CellCoord> One;
	One.Add(Cell);
	return ReleaseCells(One, bOnlyIfOwnedBy) > 0;
}

int32 USimGrid_TerritoryComponent::GetOwnedCellCount(FGameplayTag OwnerId) const
{
	if (!OwnerId.IsValid())
	{
		return Ownership.Entries.Num();
	}
	int32 Count = 0;
	for (const FSimGrid_CellOwnershipEntry& Entry : Ownership.Entries)
	{
		if (Entry.OwnerId == OwnerId)
		{
			++Count;
		}
	}
	return Count;
}

TArray<FSeam_CellCoord> USimGrid_TerritoryComponent::GetCellsOwnedBy(FGameplayTag OwnerId) const
{
	TArray<FSeam_CellCoord> Result;
	for (const FSimGrid_CellOwnershipEntry& Entry : Ownership.Entries)
	{
		if (!OwnerId.IsValid() || Entry.OwnerId == OwnerId)
		{
			Result.Add(Entry.Cell);
		}
	}
	return Result;
}

//~ ISimGrid_OwnershipRead ---------------------------------------------------------------------

FGameplayTag USimGrid_TerritoryComponent::GetCellOwner_Implementation(const FSeam_CellCoord& Cell) const
{
	const int32 Index = FindEntryIndex(Cell);
	return (Index != INDEX_NONE) ? Ownership.Entries[Index].OwnerId : FGameplayTag();
}

bool USimGrid_TerritoryComponent::IsOwnedBy_Implementation(const FSeam_CellCoord& Cell, const FGameplayTag& OwnerId) const
{
	if (!OwnerId.IsValid())
	{
		return false;
	}
	const int32 Index = FindEntryIndex(Cell);
	return Index != INDEX_NONE && Ownership.Entries[Index].OwnerId == OwnerId;
}

bool USimGrid_TerritoryComponent::IsOwnershipKnown_Implementation(const FSeam_CellCoord& /*Cell*/) const
{
	// The whole ownership fast-array replicates to relevant clients; once the component has begun play
	// and replicated, every cell's ownership state (owned or absent==unowned) is locally known. On the
	// server this is trivially true. We report known whenever the component is active.
	return true;
}

//~ Change notification ------------------------------------------------------------------------

void USimGrid_TerritoryComponent::HandleReplicatedChange()
{
	// On clients the fast-array entries arrived/changed; rebuild the read index and surface the change.
	RebuildIndex();
	NotifyTerritoryChanged();
}

void USimGrid_TerritoryComponent::NotifyTerritoryChanged_Implementation()
{
	OnTerritoryChanged.Broadcast(this);
}
