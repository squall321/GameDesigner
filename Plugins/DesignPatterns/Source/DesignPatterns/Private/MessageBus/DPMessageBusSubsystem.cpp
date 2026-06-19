// Copyright DesignPatterns plugin. All Rights Reserved.

#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPLog.h"
#include "Stats/Stats.h"

DECLARE_DWORD_ACCUMULATOR_STAT(TEXT("MessageBus Listeners"), STAT_DPBusListeners, STATGROUP_DesignPatterns);
DECLARE_CYCLE_STAT(TEXT("MessageBus Broadcast"), STAT_DPBusBroadcast, STATGROUP_DesignPatterns);

void UDP_MessageBusSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Drain the deferred queue once per frame. FTSTicker runs on the game thread and is
	// independent of any world tick, so it survives seamless travel without ticking in
	// editor preview worlds (we simply have no listeners there).
	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UDP_MessageBusSubsystem::TickDrain));
}

void UDP_MessageBusSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	Channels.Reset();
	EntriesByHandle.Reset();
	DeferredQueue.Reset();
	Super::Deinitialize();
}

FDP_ListenerHandle UDP_MessageBusSubsystem::RegisterEntry(TSharedPtr<FListenerEntry> Entry)
{
	Entry->Handle.Id = NextHandleId++;
	Channels.FindOrAdd(Entry->Channel).Add(Entry);
	EntriesByHandle.Add(Entry->Handle, Entry);
	SET_DWORD_STAT(STAT_DPBusListeners, EntriesByHandle.Num());
	return Entry->Handle;
}

FDP_ListenerHandle UDP_MessageBusSubsystem::ListenNative(
	FGameplayTag Channel,
	TFunction<void(const FDP_Message&)> Handler,
	UObject* OwnerForLifetime,
	EDP_MessageMatch Match)
{
	if (!Channel.IsValid() || !Handler)
	{
		UE_LOG(LogDPBus, Warning, TEXT("ListenNative ignored: invalid channel or null handler."));
		return FDP_ListenerHandle();
	}

	TSharedPtr<FListenerEntry> Entry = MakeShared<FListenerEntry>();
	Entry->Channel = Channel;
	Entry->Match = Match;
	Entry->Owner = OwnerForLifetime;
	Entry->NativeHandler = MoveTemp(Handler);
	Entry->bIsNative = true;
	return RegisterEntry(Entry);
}

FDP_ListenerHandle UDP_MessageBusSubsystem::ListenBP(
	FGameplayTag Channel,
	FDP_MessageDynamicDelegate Event,
	EDP_MessageMatch Match)
{
	if (!Channel.IsValid() || !Event.IsBound())
	{
		UE_LOG(LogDPBus, Warning, TEXT("ListenBP ignored: invalid channel or unbound delegate."));
		return FDP_ListenerHandle();
	}

	TSharedPtr<FListenerEntry> Entry = MakeShared<FListenerEntry>();
	Entry->Channel = Channel;
	Entry->Match = Match;
	Entry->Owner = Event.GetUObject();   // lifetime follows the bound object
	Entry->BPDelegate = Event;
	Entry->bIsNative = false;
	return RegisterEntry(Entry);
}

bool UDP_MessageBusSubsystem::DoesEntryMatch(const FListenerEntry& Entry, const FGameplayTag& BroadcastChannel) const
{
	if (Entry.Match == EDP_MessageMatch::Exact)
	{
		return Entry.Channel == BroadcastChannel;
	}
	// ExactOrChild: a listener on a parent tag receives child broadcasts.
	return BroadcastChannel.MatchesTag(Entry.Channel);
}

void UDP_MessageBusSubsystem::Broadcast(const FDP_Message& Message)
{
	if (!Message.Channel.IsValid())
	{
		return;
	}

	// Reentrancy guard: a handler that broadcasts again is deferred to next frame.
	if (bBroadcasting)
	{
		QueueBroadcast(Message);
		return;
	}

	SCOPE_CYCLE_COUNTER(STAT_DPBusBroadcast);
	TGuardValue<bool> Guard(bBroadcasting, true);

	bool bAnyInvalid = false;

	// A broadcast on DP.Bus.Combat.Damage must reach listeners on DP.Bus.Combat.Damage,
	// DP.Bus.Combat and DP.Bus. Walk every channel bucket and test the match rule; the
	// number of distinct subscribed tags is small, so this is cheap and correct.
	for (TPair<FGameplayTag, TArray<TSharedPtr<FListenerEntry>>>& ChannelPair : Channels)
	{
		// Snapshot the array: a handler may add/remove listeners mid-broadcast.
		TArray<TSharedPtr<FListenerEntry>> Snapshot = ChannelPair.Value;
		for (const TSharedPtr<FListenerEntry>& Entry : Snapshot)
		{
			if (!Entry.IsValid())
			{
				continue;
			}
			if (!Entry->Owner.IsValid())
			{
				bAnyInvalid = true;
				continue;
			}
			if (!DoesEntryMatch(*Entry, Message.Channel))
			{
				continue;
			}

			if (Entry->bIsNative)
			{
				if (Entry->NativeHandler)
				{
					Entry->NativeHandler(Message);
				}
			}
			else if (Entry->BPDelegate.IsBound())
			{
				Entry->BPDelegate.Execute(Message.Channel, Message.Payload, Message.Instigator.Get());
			}
		}
	}

	if (bAnyInvalid)
	{
		PruneInvalid();
	}
}

void UDP_MessageBusSubsystem::BroadcastPayload(FGameplayTag Channel, const FInstancedStruct& Payload, UObject* Instigator)
{
	Broadcast(FDP_Message(Channel, Payload, Instigator));
}

void UDP_MessageBusSubsystem::BroadcastBP(FGameplayTag Channel, FInstancedStruct Payload, UObject* Instigator)
{
	Broadcast(FDP_Message(Channel, Payload, Instigator));
}

void UDP_MessageBusSubsystem::QueueBroadcast(const FDP_Message& Message)
{
	DeferredQueue.Add(Message);
}

void UDP_MessageBusSubsystem::QueueBroadcastBP(FGameplayTag Channel, FInstancedStruct Payload, UObject* Instigator)
{
	QueueBroadcast(FDP_Message(Channel, Payload, Instigator));
}

bool UDP_MessageBusSubsystem::TickDrain(float /*DeltaTime*/)
{
	DrainDeferred();
	return true; // keep ticking
}

void UDP_MessageBusSubsystem::DrainDeferred()
{
	if (DeferredQueue.Num() == 0)
	{
		return;
	}
	// Swap out so messages queued during draining go to next frame, not an infinite loop.
	TArray<FDP_Message> ToDispatch = MoveTemp(DeferredQueue);
	DeferredQueue.Reset();
	for (const FDP_Message& Msg : ToDispatch)
	{
		Broadcast(Msg);
	}
}

void UDP_MessageBusSubsystem::StopListening(FDP_ListenerHandle Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}
	if (TSharedPtr<FListenerEntry> Entry = EntriesByHandle.FindRef(Handle))
	{
		if (TArray<TSharedPtr<FListenerEntry>>* Bucket = Channels.Find(Entry->Channel))
		{
			Bucket->RemoveAll([&Handle](const TSharedPtr<FListenerEntry>& E)
			{
				return E.IsValid() && E->Handle == Handle;
			});
			if (Bucket->Num() == 0)
			{
				Channels.Remove(Entry->Channel);
			}
		}
		EntriesByHandle.Remove(Handle);
		SET_DWORD_STAT(STAT_DPBusListeners, EntriesByHandle.Num());
	}
}

void UDP_MessageBusSubsystem::StopListeningForOwner(UObject* Owner)
{
	if (!Owner)
	{
		return;
	}
	TArray<FDP_ListenerHandle> ToRemove;
	for (const TPair<FDP_ListenerHandle, TSharedPtr<FListenerEntry>>& Pair : EntriesByHandle)
	{
		if (Pair.Value.IsValid() && Pair.Value->Owner.Get() == Owner)
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FDP_ListenerHandle& H : ToRemove)
	{
		StopListening(H);
	}
}

void UDP_MessageBusSubsystem::PruneInvalid()
{
	TArray<FDP_ListenerHandle> ToRemove;
	for (const TPair<FDP_ListenerHandle, TSharedPtr<FListenerEntry>>& Pair : EntriesByHandle)
	{
		if (!Pair.Value.IsValid() || !Pair.Value->Owner.IsValid())
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (const FDP_ListenerHandle& H : ToRemove)
	{
		StopListening(H);
	}
	if (ToRemove.Num() > 0)
	{
		UE_LOG(LogDPBus, Verbose, TEXT("Pruned %d stale message-bus listeners."), ToRemove.Num());
	}
}

int32 UDP_MessageBusSubsystem::GetListenerCount() const
{
	return EntriesByHandle.Num();
}

FString UDP_MessageBusSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("MessageBus: %d listeners across %d channels, %d queued"),
		EntriesByHandle.Num(), Channels.Num(), DeferredQueue.Num());
}

void UDP_MessageBusSubsystem::DumpListeners() const
{
	UE_LOG(LogDPBus, Log, TEXT("=== Message Bus: %d channels ==="), Channels.Num());
	for (const TPair<FGameplayTag, TArray<TSharedPtr<FListenerEntry>>>& Pair : Channels)
	{
		UE_LOG(LogDPBus, Log, TEXT("  Channel %s : %d listeners"), *Pair.Key.ToString(), Pair.Value.Num());
		for (const TSharedPtr<FListenerEntry>& E : Pair.Value)
		{
			if (E.IsValid())
			{
				const UObject* Owner = E->Owner.Get();
				UE_LOG(LogDPBus, Log, TEXT("    - %s owner=%s match=%d"),
					E->bIsNative ? TEXT("C++") : TEXT("BP"),
					Owner ? *Owner->GetName() : TEXT("<stale>"),
					(int32)E->Match);
			}
		}
	}
}
