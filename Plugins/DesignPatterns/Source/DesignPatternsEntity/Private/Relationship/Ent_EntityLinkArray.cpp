// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Relationship/Ent_EntityLinkArray.h"
#include "Relationship/Ent_RelationshipComponent.h"

//~ FEnt_EntityLink replication callbacks (client side) ---------------------------------------
// Each forwards to the owning component so it can re-register the world relationship index and
// broadcast a change. The owner back-pointer is wired in the component ctor on server and clients.

void FEnt_EntityLink::PreReplicatedRemove(const FEnt_EntityLinkArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedLinkChange();
	}
}

void FEnt_EntityLink::PostReplicatedAdd(const FEnt_EntityLinkArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedLinkChange();
	}
}

void FEnt_EntityLink::PostReplicatedChange(const FEnt_EntityLinkArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedLinkChange();
	}
}
