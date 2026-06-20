// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Economy/SimEco_EconomySubsystem.h"
#include "Economy/SimEco_StepListener.h"
#include "Market/SimEco_MarketSubsystem.h"
#include "Market/SimEco_Market.h"
#include "Market/SimEco_EconomyTags.h"
#include "Settings/SimEco_DeveloperSettings.h"
#include "DesignPatternsSimEconomyModule.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

void USimEco_EconomySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// Snapshot the fixed-step size from developer settings (a tunable, not a magic number).
	if (const USimEco_DeveloperSettings* Dev = USimEco_DeveloperSettings::Get())
	{
		FixedStepSeconds = FMath::Max(0.01, Dev->DefaultFixedStepSeconds);
	}

	// The driver is authority-only: clients advance nothing (they observe replicated state).
	if (HasWorldAuthority())
	{
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &USimEco_EconomySubsystem::TickAccumulator));
	}
}

void USimEco_EconomySubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	StepListeners.Reset();
	InjectedClock = nullptr;
	CachedMarket.Reset();
	Accumulator = 0.0;

	Super::Deinitialize();
}

void USimEco_EconomySubsystem::RegisterStepListener(const TScriptInterface<ISimEco_StepListener>& Listener)
{
	if (!HasWorldAuthority() || !Listener.GetObject())
	{
		return;
	}
	StepListeners.AddUnique(Listener);
}

void USimEco_EconomySubsystem::UnregisterStepListener(const TScriptInterface<ISimEco_StepListener>& Listener)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	StepListeners.RemoveAll(
		[&Listener](const TScriptInterface<ISimEco_StepListener>& Existing)
		{
			return Existing.GetObject() == Listener.GetObject();
		});
}

void USimEco_EconomySubsystem::SetSimClock(const TScriptInterface<ISeam_SimClock>& InClock)
{
	if (!HasWorldAuthority())
	{
		return;
	}
	InjectedClock = InClock;
}

TScriptInterface<ISeam_SimClock> USimEco_EconomySubsystem::ResolveClock() const
{
	if (InjectedClock.GetObject())
	{
		return InjectedClock;
	}

	// Resolve the shared clock seam published under the well-known service key.
	if (UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		if (UObject* Provider = Locator->ResolveService(SimEcoEconomyTags::Service_SimClock))
		{
			if (Provider->GetClass()->ImplementsInterface(USeam_SimClock::StaticClass()))
			{
				TScriptInterface<ISeam_SimClock> Clock;
				Clock.SetObject(Provider);
				Clock.SetInterface(Cast<ISeam_SimClock>(Provider));
				return Clock;
			}
		}
	}
	return TScriptInterface<ISeam_SimClock>();
}

USimEco_MarketSubsystem* USimEco_EconomySubsystem::ResolveMarket()
{
	if (CachedMarket.IsValid())
	{
		return CachedMarket.Get();
	}
	USimEco_MarketSubsystem* Market =
		FDP_SubsystemStatics::GetWorldSubsystem<USimEco_MarketSubsystem>(this);
	CachedMarket = Market;
	return Market;
}

bool USimEco_EconomySubsystem::TickAccumulator(float RealDeltaSeconds)
{
	// AUTHORITY GUARD: belt-and-braces — the ticker is only added on authority, but a net-mode change
	// (e.g. listen->client during travel) must not let the accumulator run.
	if (!HasWorldAuthority())
	{
		return true; // keep the ticker alive; Deinitialize removes it.
	}

	const TScriptInterface<ISeam_SimClock> Clock = ResolveClock();

	double TimeScale = 1.0;
	bool bPaused = false;
	if (Clock.GetObject() && Clock.GetInterface())
	{
		// Read time scale / pause through the seam's BlueprintNativeEvent dispatch (Execute_).
		bPaused = ISeam_SimClock::Execute_IsPaused(Clock.GetObject());
		TimeScale = ISeam_SimClock::Execute_GetTimeScale(Clock.GetObject());
	}

	// Honour pause uniformly: do not advance the accumulator while paused.
	if (!bPaused && TimeScale > 0.0)
	{
		const double ScaledDelta = static_cast<double>(RealDeltaSeconds) * TimeScale;
		Accumulator += ScaledDelta;

		// Run whole fixed steps; cap iterations so a long hitch cannot stall the game thread.
		int32 Guard = 0;
		const int32 MaxStepsPerFrame = 16;
		while (Accumulator >= FixedStepSeconds && Guard < MaxStepsPerFrame)
		{
			Accumulator -= FixedStepSeconds;
			AdvanceOneStep();
			++Guard;
		}

		// If we hit the cap, drop the backlog so we never spiral (deterministic catch-up is not a goal).
		if (Guard >= MaxStepsPerFrame && Accumulator > FixedStepSeconds)
		{
			Accumulator = FMath::Fmod(Accumulator, FixedStepSeconds);
		}
	}

	return true; // keep ticking
}

void USimEco_EconomySubsystem::AdvanceOneStep()
{
	const TScriptInterface<ISeam_SimClock> Clock = ResolveClock();
	int32 DayNumber = 0;
	if (Clock.GetObject() && Clock.GetInterface())
	{
		DayNumber = ISeam_SimClock::Execute_GetDayNumber(Clock.GetObject());
	}

	// 1) Advance every registered participant by one fixed step (production/consumption into stockpiles),
	//    pruning any that have been GC'd. Done BEFORE clearing so this step's output feeds the market.
	for (int32 Index = StepListeners.Num() - 1; Index >= 0; --Index)
	{
		UObject* Obj = StepListeners[Index].GetObject();
		ISimEco_StepListener* Listener = StepListeners[Index].GetInterface();
		if (!Obj || !Listener)
		{
			StepListeners.RemoveAtSwap(Index);
			continue;
		}
		Listener->AdvanceEconomyStep(FixedStepSeconds, StepIndex, DayNumber);
	}

	// 2) Clear the market with the freshly-updated supply/demand.
	if (USimEco_MarketSubsystem* Market = ResolveMarket())
	{
		ISimEco_Market::Execute_ClearMarket(Market);
	}

	// 3) Announce the completed step on the local bus.
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FSimEco_TickCompletedMsg Msg;
		Msg.StepIndex = StepIndex;
		Msg.StepSeconds = FixedStepSeconds;
		Msg.DayNumber = DayNumber;

		FInstancedStruct Payload;
		Payload.InitializeAs<FSimEco_TickCompletedMsg>(Msg);
		Bus->BroadcastPayload(SimEcoNativeTags::Bus_TickCompleted, Payload, this);
	}

	++StepIndex;
}

FString USimEco_EconomySubsystem::GetDPDebugString_Implementation() const
{
	const TScriptInterface<ISeam_SimClock> Clock = const_cast<USimEco_EconomySubsystem*>(this)->ResolveClock();
	return FString::Printf(TEXT("SimEco Driver: step=%lld listeners=%d acc=%.3f/%.3f clock=%s"),
		StepIndex, StepListeners.Num(), Accumulator, FixedStepSeconds,
		Clock.GetObject() ? TEXT("bound") : TEXT("none"));
}
