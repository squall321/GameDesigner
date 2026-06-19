// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Command/DPCommandQueueComponent.h"
#include "Command/DPGameplayCommand.h"
#include "Command/DPCommandHistorySubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

UDP_CommandQueueComponent::UDP_CommandQueueComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Local-only intent bridge: no replication. A networked variant overrides ForwardToHistory.
	SetIsReplicatedByDefault(false);
}

void UDP_CommandQueueComponent::BeginPlay()
{
	Super::BeginPlay();
	// Warm the cache so the first SubmitCommand on input is cheap.
	GetHistory();
}

UDP_CommandHistorySubsystem* UDP_CommandQueueComponent::GetHistory() const
{
	if (CachedHistory.IsValid())
	{
		return CachedHistory.Get();
	}
	UDP_CommandHistorySubsystem* History =
		FDP_SubsystemStatics::GetWorldSubsystem<UDP_CommandHistorySubsystem>(this);
	CachedHistory = History;
	return History;
}

FDP_CommandContext UDP_CommandQueueComponent::MakeContextForOwner(FInstancedStruct Params) const
{
	FDP_CommandContext Context;
	if (const AActor* Owner = GetOwner())
	{
		Context.Target.Set(Owner);
		Context.Instigator.Set(Owner);
	}
	Context.Params = MoveTemp(Params);
	return Context;
}

bool UDP_CommandQueueComponent::SubmitCommand(UDP_GameplayCommand* Command)
{
	return SubmitCommandWithContext(Command, MakeContextForOwner(FInstancedStruct()));
}

bool UDP_CommandQueueComponent::SubmitCommandWithContext(UDP_GameplayCommand* Command, const FDP_CommandContext& Context)
{
	if (!IsValid(Command))
	{
		UE_LOG(LogDPCmd, Warning, TEXT("%s: SubmitCommand ignored (null command)."), *GetNameSafe(GetOwner()));
		return false;
	}
	return ForwardToHistory(Command, Context);
}

UDP_GameplayCommand* UDP_CommandQueueComponent::MakeAndSubmit(TSubclassOf<UDP_GameplayCommand> CommandClass, FInstancedStruct Params)
{
	if (!CommandClass)
	{
		UE_LOG(LogDPCmd, Warning, TEXT("%s: MakeAndSubmit ignored (null class)."), *GetNameSafe(GetOwner()));
		return nullptr;
	}

	// Owned by this component until the history subsystem re-outers it on a recorded submit.
	UDP_GameplayCommand* Command = NewObject<UDP_GameplayCommand>(this, CommandClass);
	const FDP_CommandContext Context = MakeContextForOwner(MoveTemp(Params));

	if (!ForwardToHistory(Command, Context))
	{
		// Not recorded — let GC collect the throwaway instance.
		return nullptr;
	}
	return Command;
}

bool UDP_CommandQueueComponent::ForwardToHistory(UDP_GameplayCommand* Command, const FDP_CommandContext& Context)
{
	UDP_CommandHistorySubsystem* History = GetHistory();
	if (!History)
	{
		UE_LOG(LogDPCmd, Warning, TEXT("%s: no CommandHistory subsystem; command '%s' dropped."),
			*GetNameSafe(GetOwner()), *Command->GetDisplayName());
		return false;
	}
	return History->Submit(Command, Context);
}
