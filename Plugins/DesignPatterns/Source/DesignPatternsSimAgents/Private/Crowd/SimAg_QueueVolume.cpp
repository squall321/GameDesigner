// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Crowd/SimAg_QueueVolume.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"

//~ FSimAg_QueueSlot fast-array callbacks (clients only) ------------------------------------------

void FSimAg_QueueSlot::PostReplicatedAdd(const FSimAg_QueueArray& InArraySerializer)
{
	if (InArraySerializer.OwnerVolume)
	{
		InArraySerializer.OwnerVolume->HandleReplicatedChange();
	}
}

void FSimAg_QueueSlot::PostReplicatedChange(const FSimAg_QueueArray& InArraySerializer)
{
	if (InArraySerializer.OwnerVolume)
	{
		InArraySerializer.OwnerVolume->HandleReplicatedChange();
	}
}

void FSimAg_QueueSlot::PreReplicatedRemove(const FSimAg_QueueArray& InArraySerializer)
{
	if (InArraySerializer.OwnerVolume)
	{
		InArraySerializer.OwnerVolume->HandleReplicatedChange();
	}
}

//~ ASimAg_QueueVolume ----------------------------------------------------------------------------

ASimAg_QueueVolume::ASimAg_QueueVolume()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	NetDormancy = DORM_Initial;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;

	Queue.OwnerVolume = this;
}

void ASimAg_QueueVolume::BeginPlay()
{
	Super::BeginPlay();
	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		SlotSpacing = FMath::Max(1.f, Settings->QueueSlotSpacing);
	}
}

void ASimAg_QueueVolume::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	Queue.OwnerVolume = this;
}

void ASimAg_QueueVolume::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASimAg_QueueVolume, Queue);
}

void ASimAg_QueueVolume::WakeForChange()
{
	if (NetDormancy > DORM_Awake)
	{
		FlushNetDormancy();
	}
}

void ASimAg_QueueVolume::SortByTicket()
{
	Queue.Slots.Sort([](const FSimAg_QueueSlot& A, const FSimAg_QueueSlot& B) { return A.Ticket < B.Ticket; });
}

int32 ASimAg_QueueVolume::Enqueue(FSeam_EntityId Agent)
{
	// AUTHORITY GUARD at top.
	if (!HasAuthority())
	{
		return INDEX_NONE;
	}
	if (!Agent.IsValid())
	{
		return INDEX_NONE;
	}

	const int32 Existing = GetPosition(Agent);
	if (Existing != INDEX_NONE)
	{
		return Existing;
	}

	Queue.Slots.Add(FSimAg_QueueSlot(Agent, NextTicket++));
	SortByTicket();
	Queue.MarkArrayDirty();
	WakeForChange();
	OnQueueChanged.Broadcast(this);
	return GetPosition(Agent);
}

bool ASimAg_QueueVolume::Dequeue(FSeam_EntityId Agent)
{
	if (!HasAuthority())
	{
		return false;
	}
	const int32 Removed = Queue.Slots.RemoveAll([&Agent](const FSimAg_QueueSlot& S) { return S.Agent == Agent; });
	if (Removed > 0)
	{
		Queue.MarkArrayDirty();
		WakeForChange();
		OnQueueChanged.Broadcast(this);
		return true;
	}
	return false;
}

int32 ASimAg_QueueVolume::GetPosition(FSeam_EntityId Agent) const
{
	return Queue.Slots.IndexOfByPredicate([&Agent](const FSimAg_QueueSlot& S) { return S.Agent == Agent; });
}

FVector ASimAg_QueueVolume::GetSlotLocation(FSeam_EntityId Agent) const
{
	const int32 Position = GetPosition(Agent);
	if (Position == INDEX_NONE)
	{
		return GetActorLocation();
	}
	// Head stands at the actor location; each subsequent slot is SlotSpacing further behind (-X local),
	// oriented by the actor's facing.
	const FVector LocalOffset(-static_cast<float>(Position) * SlotSpacing, 0.f, 0.f);
	return GetActorLocation() + GetActorRotation().RotateVector(LocalOffset);
}

FSeam_EntityId ASimAg_QueueVolume::GetHead() const
{
	return Queue.Slots.Num() > 0 ? Queue.Slots[0].Agent : FSeam_EntityId::Invalid();
}

void ASimAg_QueueVolume::HandleReplicatedChange()
{
	OnQueueChanged.Broadcast(this);
}
