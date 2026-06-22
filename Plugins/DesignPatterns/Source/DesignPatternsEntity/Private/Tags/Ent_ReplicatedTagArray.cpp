// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Tags/Ent_ReplicatedTagArray.h"
#include "Tags/Ent_TagContainerComponent.h"

//~ FEnt_ReplicatedTag replication callbacks (client side) ------------------------------------

void FEnt_ReplicatedTag::PreReplicatedRemove(const FEnt_ReplicatedTagArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedTagChange(Tag);
	}
}

void FEnt_ReplicatedTag::PostReplicatedAdd(const FEnt_ReplicatedTagArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedTagChange(Tag);
	}
}

void FEnt_ReplicatedTag::PostReplicatedChange(const FEnt_ReplicatedTagArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedTagChange(Tag);
	}
}
