// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Replication/WorldHub_StateRepComponent.h"
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

//~ FWorldHub_RepEntry replication callbacks (client side) ------------------------------------

void FWorldHub_RepEntry::PreReplicatedRemove(const FWorldHub_RepStateArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		// A removed entry: clear it locally. Value is irrelevant for a removal.
		InArraySerializer.OwnerComponent->HandleReplicatedChange(Scope, Key, FSeam_NetValue(), /*bRemoved=*/true);
	}
}

void FWorldHub_RepEntry::PostReplicatedAdd(const FWorldHub_RepStateArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange(Scope, Key, Value, /*bRemoved=*/false);
	}
}

void FWorldHub_RepEntry::PostReplicatedChange(const FWorldHub_RepStateArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange(Scope, Key, Value, /*bRemoved=*/false);
	}
}

//~ UWorldHub_StateRepComponent ----------------------------------------------------------------

UWorldHub_StateRepComponent::UWorldHub_StateRepComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// Wire the fast-array back-pointer so per-entry callbacks can notify us (server and client).
	RepArray.OwnerComponent = this;
}

void UWorldHub_StateRepComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UWorldHub_StateRepComponent, RepArray);
}

bool UWorldHub_StateRepComponent::HasAuthorityToMutate() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

UWorldHub_StateHubSubsystem* UWorldHub_StateRepComponent::ResolveHub() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this);
}

int32 UWorldHub_StateRepComponent::IndexOf(const FWorldHub_Scope& Scope, const FGameplayTag& Key) const
{
	for (int32 Index = 0; Index < RepArray.Entries.Num(); ++Index)
	{
		const FWorldHub_RepEntry& Entry = RepArray.Entries[Index];
		if (Entry.Key == Key && Entry.Scope == Scope)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UWorldHub_StateRepComponent::BeginPlay()
{
	Super::BeginPlay();

	// Resolve the local hub and attach so it mirrors authoritative writes here.
	if (UWorldHub_StateHubSubsystem* Hub = ResolveHub())
	{
		Hub->AttachNetCarrier(this);

		if (HasAuthorityToMutate())
		{
			// AUTHORITY: seed the carrier from the hub's current replicable state.
			FWorldHub_RepState State;
			Hub->BuildRepState(State);
			Authority_ReplaceAll(State.Entries);
		}
		else
		{
			// CLIENT: any entries already replicated before BeginPlay should be pushed into the hub.
			TArray<FWorldHub_ScopedRepEntry> Current;
			BuildScopedEntries(Current);
			Hub->SyncReplicatedState(FWorldHub_RepState(Current));
		}
	}
	else
	{
		UE_LOG(LogDP, Warning, TEXT("[WorldHub] StateRepComponent on '%s' could not resolve the world hub at BeginPlay."),
			*GetNameSafe(GetOwner()));
	}
}

void UWorldHub_StateRepComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorldHub_StateHubSubsystem* Hub = ResolveHub())
	{
		Hub->DetachNetCarrier(this);
	}
	Super::EndPlay(EndPlayReason);
}

bool UWorldHub_StateRepComponent::Authority_SetEntry(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FSeam_NetValue& Value)
{
	// AUTHORITY GUARD at the TOP: never mutate replicated state on a client.
	if (!HasAuthorityToMutate())
	{
		return false;
	}
	if (!Key.IsValid() || !Value.IsSet())
	{
		return false;
	}

	const int32 Index = IndexOf(Scope, Key);
	if (Index != INDEX_NONE)
	{
		FWorldHub_RepEntry& Entry = RepArray.Entries[Index];
		if (Entry.Value == Value)
		{
			return false;
		}
		Entry.Value = Value;
		RepArray.MarkItemDirty(Entry);
		return true;
	}

	FWorldHub_RepEntry& NewEntry = RepArray.Entries.AddDefaulted_GetRef();
	NewEntry.Scope = Scope;
	NewEntry.Key = Key;
	NewEntry.Value = Value;
	RepArray.MarkItemDirty(NewEntry);
	return true;
}

bool UWorldHub_StateRepComponent::Authority_RemoveEntry(const FWorldHub_Scope& Scope, const FGameplayTag& Key)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasAuthorityToMutate())
	{
		return false;
	}

	const int32 Index = IndexOf(Scope, Key);
	if (Index == INDEX_NONE)
	{
		return false;
	}
	RepArray.Entries.RemoveAt(Index);
	RepArray.MarkArrayDirty();
	return true;
}

void UWorldHub_StateRepComponent::Authority_ReplaceAll(const TArray<FWorldHub_ScopedRepEntry>& NewEntries)
{
	// AUTHORITY GUARD at the TOP.
	if (!HasAuthorityToMutate())
	{
		return;
	}

	RepArray.Entries.Reset(NewEntries.Num());
	for (const FWorldHub_ScopedRepEntry& Src : NewEntries)
	{
		if (!Src.Key.IsValid() || !Src.Value.IsSet())
		{
			continue;
		}
		FWorldHub_RepEntry& Entry = RepArray.Entries.AddDefaulted_GetRef();
		Entry.Scope = Src.Scope;
		Entry.Key = Src.Key;
		Entry.Value = Src.Value;
	}
	RepArray.MarkArrayDirty();
}

void UWorldHub_StateRepComponent::BuildScopedEntries(TArray<FWorldHub_ScopedRepEntry>& Out) const
{
	Out.Reset(RepArray.Entries.Num());
	for (const FWorldHub_RepEntry& Entry : RepArray.Entries)
	{
		Out.Emplace(Entry.Scope, Entry.Key, Entry.Value);
	}
}

void UWorldHub_StateRepComponent::HandleReplicatedChange(const FWorldHub_Scope& Scope, const FGameplayTag& Key, const FSeam_NetValue& Value, bool bRemoved)
{
	// CLIENT path: push the full current set into the hub so its registry + listeners mirror the
	// authoritative state. A full snapshot keeps the hub's pruning of stale replicated slots correct
	// even when several entries change in one replication burst.
	UWorldHub_StateHubSubsystem* Hub = ResolveHub();
	if (!Hub)
	{
		return;
	}

	TArray<FWorldHub_ScopedRepEntry> Current;
	BuildScopedEntries(Current);
	Hub->SyncReplicatedState(FWorldHub_RepState(Current));
}
