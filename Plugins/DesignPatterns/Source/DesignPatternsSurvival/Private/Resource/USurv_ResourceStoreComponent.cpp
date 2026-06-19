// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Resource/USurv_ResourceStoreComponent.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

USurv_ResourceStoreComponent::USurv_ResourceStoreComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void USurv_ResourceStoreComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USurv_ResourceStoreComponent, Stacks);
}

bool USurv_ResourceStoreComponent::HasAuthorityToMutate() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

int32 USurv_ResourceStoreComponent::IndexOf(const FGameplayTag& ItemTag) const
{
	for (int32 i = 0; i < Stacks.Num(); ++i)
	{
		if (Stacks[i].ItemTag == ItemTag)
		{
			return i;
		}
	}
	return INDEX_NONE;
}

int32 USurv_ResourceStoreComponent::GetCount(FGameplayTag ItemTag) const
{
	const int32 Index = IndexOf(ItemTag);
	return Index == INDEX_NONE ? 0 : Stacks[Index].Count;
}

bool USurv_ResourceStoreComponent::HasAtLeast(FGameplayTag ItemTag, int32 Amount) const
{
	return GetCount(ItemTag) >= Amount;
}

int32 USurv_ResourceStoreComponent::AddResource(FGameplayTag ItemTag, int32 Amount)
{
	if (!HasAuthorityToMutate())
	{
		return GetCount(ItemTag);
	}
	if (!ItemTag.IsValid() || Amount <= 0)
	{
		return GetCount(ItemTag);
	}

	int32 Index = IndexOf(ItemTag);
	if (Index == INDEX_NONE)
	{
		Index = Stacks.Add(FSurv_ResourceStack(ItemTag, 0));
	}
	Stacks[Index].Count += Amount;

	UE_LOG(LogDP, Verbose, TEXT("[Survival] +%d %s -> %d"), Amount, *ItemTag.ToString(), Stacks[Index].Count);
	BroadcastDeltas();
	return Stacks[Index].Count;
}

bool USurv_ResourceStoreComponent::RemoveResource(FGameplayTag ItemTag, int32 Amount)
{
	if (!HasAuthorityToMutate())
	{
		return false;
	}
	if (!ItemTag.IsValid() || Amount <= 0)
	{
		return false;
	}

	const int32 Index = IndexOf(ItemTag);
	if (Index == INDEX_NONE || Stacks[Index].Count < Amount)
	{
		return false;
	}

	Stacks[Index].Count -= Amount;
	if (Stacks[Index].Count <= 0)
	{
		Stacks.RemoveAt(Index);
	}

	UE_LOG(LogDP, Verbose, TEXT("[Survival] -%d %s"), Amount, *ItemTag.ToString());
	BroadcastDeltas();
	return true;
}

void USurv_ResourceStoreComponent::OnRep_Stacks()
{
	// Clients observe replicated changes here and translate them into the same per-stack events.
	BroadcastDeltas();
}

void USurv_ResourceStoreComponent::BroadcastDeltas()
{
	// Fire OnResourceChanged for every tag whose count changed since the last broadcast.
	for (const FSurv_ResourceStack& Now : Stacks)
	{
		int32 Old = 0;
		for (const FSurv_ResourceStack& Prev : LastBroadcastStacks)
		{
			if (Prev.ItemTag == Now.ItemTag)
			{
				Old = Prev.Count;
				break;
			}
		}
		if (Old != Now.Count)
		{
			OnResourceChanged.Broadcast(Now.ItemTag, Now.Count);
		}
	}

	// Tags that vanished from Stacks went to zero.
	for (const FSurv_ResourceStack& Prev : LastBroadcastStacks)
	{
		if (IndexOf(Prev.ItemTag) == INDEX_NONE && Prev.Count != 0)
		{
			OnResourceChanged.Broadcast(Prev.ItemTag, 0);
		}
	}

	LastBroadcastStacks = Stacks;
}
