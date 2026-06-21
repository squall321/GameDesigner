// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Events/SimEco_EventReplicationProxy.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"

void FSimEco_ActiveEventEntry::PostReplicatedAdd(const FSimEco_ActiveEventArray& In)    { if (In.OwnerProxy) In.OwnerProxy->HandleReplicatedChange(); }
void FSimEco_ActiveEventEntry::PostReplicatedChange(const FSimEco_ActiveEventArray& In) { if (In.OwnerProxy) In.OwnerProxy->HandleReplicatedChange(); }
void FSimEco_ActiveEventEntry::PreReplicatedRemove(const FSimEco_ActiveEventArray& In)  { if (In.OwnerProxy) In.OwnerProxy->HandleReplicatedChange(); }

ASimEco_EventReplicationProxy::ASimEco_EventReplicationProxy()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicatingMovement(false);
	PrimaryActorTick.bCanEverTick = false;
	NetUpdateFrequency = 2.0f;
	Active.OwnerProxy = this;
}

void ASimEco_EventReplicationProxy::PostInitProperties()
{
	Super::PostInitProperties();
	Active.OwnerProxy = this;
}

void ASimEco_EventReplicationProxy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ASimEco_EventReplicationProxy, Active);
}

void ASimEco_EventReplicationProxy::SetActiveEvents(const TArray<FSimEco_ActiveEventEntry>& InEntries)
{
	if (!HasAuthority())
	{
		return;
	}
	Active.Entries = InEntries;
	Active.MarkArrayDirty();
	OnActiveEventsChanged.Broadcast(this);
}

void ASimEco_EventReplicationProxy::HandleReplicatedChange()
{
	OnActiveEventsChanged.Broadcast(this);
}
