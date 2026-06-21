// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Events/SimEco_EconomicEventSubsystem.h"
#include "Events/SimEco_EconomicEventDef.h"
#include "Events/SimEco_EventReplicationProxy.h"
#include "Pricing/SimEco_PricingTags.h"
#include "Economy/SimEco_EconomySubsystem.h"
#include "Market/SimEco_MarketSubsystem.h"
#include "Market/SimEco_Market.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

void USimEco_EconomicEventSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	if (HasWorldAuthority())
	{
		EnsureProxy();
		if (USimEco_EconomySubsystem* Eco = ResolveEconomy())
		{
			TScriptInterface<ISimEco_StepListener> Self;
			Self.SetObject(this);
			Self.SetInterface(static_cast<ISimEco_StepListener*>(this));
			Eco->RegisterStepListener(Self);
			bRegisteredWithEconomy = true;
		}
	}
}

void USimEco_EconomicEventSubsystem::Deinitialize()
{
	if (bRegisteredWithEconomy)
	{
		if (USimEco_EconomySubsystem* Eco = ResolveEconomy())
		{
			TScriptInterface<ISimEco_StepListener> Self;
			Self.SetObject(this);
			Self.SetInterface(static_cast<ISimEco_StepListener*>(this));
			Eco->UnregisterStepListener(Self);
		}
		bRegisteredWithEconomy = false;
	}
	if (HasWorldAuthority() && Proxy.IsValid())
	{
		Proxy->Destroy();
	}
	Proxy.Reset();
	LiveEvents.Reset();
	Super::Deinitialize();
}

void USimEco_EconomicEventSubsystem::EnsureProxy()
{
	if (!HasWorldAuthority() || Proxy.IsValid())
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	Proxy = World->SpawnActor<ASimEco_EventReplicationProxy>(
		ASimEco_EventReplicationProxy::StaticClass(), FTransform::Identity, Params);
}

USimEco_EconomySubsystem* USimEco_EconomicEventSubsystem::ResolveEconomy() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimEco_EconomySubsystem>(this);
}

USimEco_MarketSubsystem* USimEco_EconomicEventSubsystem::ResolveMarket() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<USimEco_MarketSubsystem>(this);
}

USimEco_EconomicEventDef* USimEco_EconomicEventSubsystem::ResolveEventDefByTag(const FGameplayTag& EventTag) const
{
	if (!EventTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Reg = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		// Event defs index by their own DataTag; projects author DataTag == EventTag for restore to work.
		return Reg->Find<USimEco_EconomicEventDef>(EventTag);
	}
	return nullptr;
}

bool USimEco_EconomicEventSubsystem::TriggerEvent(USimEco_EconomicEventDef* EventDef)
{
	if (!HasWorldAuthority() || !EventDef || !EventDef->EventTag.IsValid())
	{
		return false;
	}

	// Refresh an already-live event rather than stacking duplicates.
	for (FSimEco_LiveEvent& E : LiveEvents)
	{
		if (E.EventTag == EventDef->EventTag)
		{
			E.RemainingSteps = FMath::Max(1, EventDef->DurationSteps);
			E.Def = EventDef;
			SyncProxy();
			NotifyEventsChanged();
			return true;
		}
	}

	FSimEco_LiveEvent New;
	New.Def = EventDef;
	New.EventTag = EventDef->EventTag;
	New.RemainingSteps = FMath::Max(1, EventDef->DurationSteps);
	LiveEvents.Add(New);

	SyncProxy();
	NotifyEventsChanged();
	UE_LOG(LogDP, Verbose, TEXT("[EcoEvent] Triggered %s for %d steps"), *EventDef->EventTag.ToString(), New.RemainingSteps);
	return true;
}

bool USimEco_EconomicEventSubsystem::EndEvent(FGameplayTag EventTag)
{
	if (!HasWorldAuthority())
	{
		return false;
	}
	const int32 Removed = LiveEvents.RemoveAll([&EventTag](const FSimEco_LiveEvent& E) { return E.EventTag == EventTag; });
	if (Removed > 0)
	{
		SyncProxy();
		NotifyEventsChanged();
		return true;
	}
	return false;
}

bool USimEco_EconomicEventSubsystem::IsEventActive(FGameplayTag EventTag) const
{
	return LiveEvents.ContainsByPredicate([&EventTag](const FSimEco_LiveEvent& E) { return E.EventTag == EventTag; });
}

void USimEco_EconomicEventSubsystem::InjectEventOrders(const FSimEco_LiveEvent& Event)
{
	USimEco_MarketSubsystem* Market = ResolveMarket();
	if (!Market || !Event.Def)
	{
		return;
	}

	const ESimEco_OrderSide Side = (Event.Def->Kind == ESimEco_EventKind::Shortage)
		? ESimEco_OrderSide::Buy     // shortage => extra demand => price up
		: ESimEco_OrderSide::Sell;   // boom => extra supply => price down

	for (const FSimEco_EventCommodity& C : Event.Def->Commodities)
	{
		if (!C.CommodityTag.IsValid() || C.SyntheticQuantityPerStep <= 0.0)
		{
			continue;
		}
		// LimitPrice 0 = market order: the synthetic flow is accepted at the clearing price, biasing it.
		FSimEco_Order Order(C.CommodityTag, Side, C.SyntheticQuantityPerStep, 0.0);
		Market->PlaceOrder_Implementation(Order);
	}
}

void USimEco_EconomicEventSubsystem::AdvanceEconomyStep(double /*StepSeconds*/, int64 /*StepIndex*/, int32 /*DayNumber*/)
{
	if (!HasWorldAuthority() || LiveEvents.Num() == 0)
	{
		return;
	}

	// Inject synthetic orders for every live event BEFORE the market clears (the driver calls listeners
	// before ClearMarket), then decrement and reap expired events.
	for (FSimEco_LiveEvent& Event : LiveEvents)
	{
		InjectEventOrders(Event);
		Event.RemainingSteps = FMath::Max(0, Event.RemainingSteps - 1);
	}

	const int32 Reaped = LiveEvents.RemoveAll([](const FSimEco_LiveEvent& E) { return E.RemainingSteps <= 0; });

	SyncProxy();
	if (Reaped > 0)
	{
		NotifyEventsChanged();
	}
}

void USimEco_EconomicEventSubsystem::SyncProxy()
{
	if (!Proxy.IsValid())
	{
		return;
	}
	TArray<FSimEco_ActiveEventEntry> Summary;
	Summary.Reserve(LiveEvents.Num());
	for (const FSimEco_LiveEvent& E : LiveEvents)
	{
		FSimEco_ActiveEventEntry Entry;
		Entry.EventTag = E.EventTag;
		Entry.RemainingSteps = E.RemainingSteps;
		Summary.Add(Entry);
	}
	Proxy->SetActiveEvents(Summary);
}

void USimEco_EconomicEventSubsystem::NotifyEventsChanged() const
{
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->BroadcastPayload(SimEcoPricingTags::Bus_EconomicEvent, FInstancedStruct(), nullptr);
	}
}

// ---- Persistence ----

void USimEco_EconomicEventSubsystem::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FSimEco_EventSaveRecord Record;
	for (const FSimEco_LiveEvent& E : LiveEvents)
	{
		FSimEco_SavedEvent Saved;
		Saved.EventTag = E.EventTag;
		Saved.RemainingSteps = E.RemainingSteps;
		Record.SavedEvents.Add(Saved);
	}
	Out.InitializeAs<FSimEco_EventSaveRecord>(Record);
}

void USimEco_EconomicEventSubsystem::RestoreState_Implementation(const FInstancedStruct& In)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	const FSimEco_EventSaveRecord* Record = In.GetPtr<FSimEco_EventSaveRecord>();
	if (!Record)
	{
		return;
	}

	LiveEvents.Reset();
	for (const FSimEco_SavedEvent& Saved : Record->SavedEvents)
	{
		if (Saved.RemainingSteps <= 0)
		{
			continue;
		}
		USimEco_EconomicEventDef* Def = ResolveEventDefByTag(Saved.EventTag);
		if (!Def)
		{
			// The def could not be re-resolved (content removed); drop the event defensively.
			UE_LOG(LogDP, Warning, TEXT("[EcoEvent] Restore: no def for event tag %s; dropped."), *Saved.EventTag.ToString());
			continue;
		}
		FSimEco_LiveEvent Live;
		Live.Def = Def;
		Live.EventTag = Saved.EventTag;
		Live.RemainingSteps = Saved.RemainingSteps;
		LiveEvents.Add(Live);
	}
	SyncProxy();
	NotifyEventsChanged();
}

FGameplayTag USimEco_EconomicEventSubsystem::GetPersistenceKind_Implementation() const
{
	return SimEcoPricingTags::Persist_Events;
}

FString USimEco_EconomicEventSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("EcoEvents auth=%d live=%d"), HasWorldAuthority() ? 1 : 0, LiveEvents.Num());
}
