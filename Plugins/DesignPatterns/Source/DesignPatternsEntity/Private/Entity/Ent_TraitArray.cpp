// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Entity/Ent_TraitArray.h"
#include "Entity/Ent_EntityComponent.h"

//~ FEnt_TraitEntry replication callbacks (client side) ---------------------------------------
// Each forwards to the owning component so it can reconcile its live trait mirror and broadcast
// a change. The owner back-pointer is wired in the component ctor on both server and clients.

void FEnt_TraitEntry::PreReplicatedRemove(const FEnt_TraitArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedTraitChange();
	}
}

void FEnt_TraitEntry::PostReplicatedAdd(const FEnt_TraitArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedTraitChange();
	}
}

void FEnt_TraitEntry::PostReplicatedChange(const FEnt_TraitArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedTraitChange();
	}
}
