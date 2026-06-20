// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Process/SimEco_ProcessComponentBase.h"
#include "Process/SimEco_ProcessDef.h"
#include "Commodity/SimEco_StockpileComponent.h"
#include "Settings/SimEco_DeveloperSettings.h"
#include "DesignPatternsSimEconomyModule.h"
#include "Market/SimEco_EconomyTags.h"
#include "Clock/Seam_SimClock.h"
#include "Net/Seam_NetValue.h"
#include "Data/DPDataRegistrySubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Services/DPServiceTypes.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

USimEco_ProcessComponentBase::USimEco_ProcessComponentBase()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void USimEco_ProcessComponentBase::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimEco_ProcessComponentBase, ActiveProcessTag);
	DOREPLIFETIME(USimEco_ProcessComponentBase, ServerCycleStartTime);
	DOREPLIFETIME(USimEco_ProcessComponentBase, ActiveCycleSeconds);
}

void USimEco_ProcessComponentBase::BeginPlay()
{
	Super::BeginPlay();

	// Clients do not run the authoritative cycle loop; they only display from replicated state.
	if (!HasAuthority())
	{
		PrimaryComponentTick.SetTickFunctionEnable(false);
	}
}

bool USimEco_ProcessComponentBase::HasAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

USimEco_StockpileComponent* USimEco_ProcessComponentBase::GetTargetStockpile() const
{
	if (StockpileOverride)
	{
		return StockpileOverride;
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<USimEco_StockpileComponent>();
	}
	return nullptr;
}

const USimEco_ProcessDef* USimEco_ProcessComponentBase::ResolveProcessDef() const
{
	if (!ActiveProcessTag.IsValid())
	{
		return nullptr;
	}
	if (UDP_DataRegistrySubsystem* Registry =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Registry->Find<USimEco_ProcessDef>(ActiveProcessTag);
	}
	return nullptr;
}

TScriptInterface<ISeam_SimClock> USimEco_ProcessComponentBase::ResolveClock() const
{
	if (ClockOverride && ClockOverride.GetObject())
	{
		return ClockOverride;
	}

	const USimEco_DeveloperSettings* Settings = USimEco_DeveloperSettings::Get();
	if (Settings && Settings->bAutoBindSurvivalClock)
	{
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
	}
	return TScriptInterface<ISeam_SimClock>();
}

double USimEco_ProcessComponentBase::GetServerTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

bool USimEco_ProcessComponentBase::StartProcess(FGameplayTag ProcessTag)
{
	// AUTHORITY GUARD: process control is server-only.
	if (!HasAuthority())
	{
		return false;
	}
	if (!ProcessTag.IsValid())
	{
		return false;
	}

	// Replace any current process cleanly (release its reservations first).
	if (ActiveProcessTag.IsValid())
	{
		CancelProcess();
	}

	// Bind the new process so ResolveProcessDef() can find it, then validate.
	ActiveProcessTag = ProcessTag;
	const USimEco_ProcessDef* Def = ResolveProcessDef();
	if (!Def)
	{
		UE_LOG(LogDP, Warning, TEXT("[SimEco_Process] StartProcess: unknown process %s"), *ProcessTag.ToString());
		ActiveProcessTag = FGameplayTag();
		return false;
	}

	// Facility gate: the site must satisfy the recipe's required facility (if any).
	if (Def->RequiredFacilityTag.IsValid() && FacilityTag != Def->RequiredFacilityTag)
	{
		UE_LOG(LogDP, Verbose, TEXT("[SimEco_Process] StartProcess: facility %s does not satisfy required %s"),
			*FacilityTag.ToString(), *Def->RequiredFacilityTag.ToString());
		ActiveProcessTag = FGameplayTag();
		return false;
	}

	ServerBeginNewCycle(*Def);
	OnRep_ActiveProcessTag(); // fire local notify on the server too
	return true;
}

void USimEco_ProcessComponentBase::CancelProcess()
{
	// AUTHORITY GUARD.
	if (!HasAuthority())
	{
		return;
	}
	if (!ActiveProcessTag.IsValid())
	{
		return;
	}

	if (const USimEco_ProcessDef* Def = ResolveProcessDef())
	{
		if (USimEco_StockpileComponent* Stockpile = GetTargetStockpile())
		{
			// Hand back anything we reserved for the in-flight cycle.
			OnProcessCancelled(*Def, *Stockpile);
		}
	}

	ActiveProcessTag = FGameplayTag();
	ServerCycleStartTime = 0.0;
	ActiveCycleSeconds = 0.0;
	ServerCycleAccumulator = 0.0;
	bServerCycleReserved = false;

	OnRep_ActiveProcessTag();
}

void USimEco_ProcessComponentBase::ServerBeginNewCycle(const USimEco_ProcessDef& Def)
{
	ServerCycleAccumulator = 0.0;
	bServerCycleReserved = false;
	ActiveCycleSeconds = FMath::Max(0.01, Def.CycleSeconds);
	ServerCycleStartTime = GetServerTimeSeconds();
}

void USimEco_ProcessComponentBase::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// AUTHORITY: only the server advances the simulation cycle.
	if (!HasAuthority() || !ActiveProcessTag.IsValid())
	{
		return;
	}

	// Convert real delta into scaled simulation time, honoring the shared clock's time-scale/pause.
	double TimeScale = 1.0;
	if (TScriptInterface<ISeam_SimClock> Clock = ResolveClock())
	{
		UObject* ClockObj = Clock.GetObject();
		if (ClockObj && ISeam_SimClock::Execute_IsPaused(ClockObj))
		{
			return; // paused: do not advance
		}
		if (ClockObj)
		{
			TimeScale = ISeam_SimClock::Execute_GetTimeScale(ClockObj);
		}
	}

	const double ScaledDelta = static_cast<double>(DeltaTime) * FMath::Max(0.0, TimeScale);
	if (ScaledDelta <= 0.0)
	{
		return;
	}

	ServerAdvance(ScaledDelta);
}

void USimEco_ProcessComponentBase::ServerAdvance(double ScaledDelta)
{
	const USimEco_ProcessDef* Def = ResolveProcessDef();
	USimEco_StockpileComponent* Stockpile = GetTargetStockpile();
	if (!Def || !Stockpile)
	{
		return;
	}

	// Reserve the cycle's inputs before time advances. If reservation fails (shortage), stall:
	// stay at the cycle start and retry next tick without consuming time.
	if (!bServerCycleReserved)
	{
		if (!OnCycleBegun(*Def, *Stockpile))
		{
			// Keep the start stamp pinned to "now" so progress reads ~0 while stalled.
			ServerCycleStartTime = GetServerTimeSeconds();
			ServerCycleAccumulator = 0.0;
			return;
		}
		bServerCycleReserved = true;
		// Anchor the visible cycle start at the moment inputs were actually secured.
		ServerCycleStartTime = GetServerTimeSeconds();
	}

	ServerCycleAccumulator += ScaledDelta;

	// Complete as many cycles as the accumulated time allows (handles large dt / high time-scale).
	int32 SafetyGuard = 0;
	while (bServerCycleReserved && ServerCycleAccumulator >= ActiveCycleSeconds && SafetyGuard < 64)
	{
		++SafetyGuard;
		ServerCycleAccumulator -= ActiveCycleSeconds;

		OnCycleCompleted(*Def, *Stockpile);
		BroadcastTickCompleted();

		// Begin the next cycle; re-reserve its inputs.
		bServerCycleReserved = false;
		if (!OnCycleBegun(*Def, *Stockpile))
		{
			// Out of inputs: stall at the start of the new cycle.
			ServerCycleStartTime = GetServerTimeSeconds();
			ServerCycleAccumulator = 0.0;
			return;
		}
		bServerCycleReserved = true;
		ServerCycleStartTime = GetServerTimeSeconds() - ServerCycleAccumulator;
	}
}

float USimEco_ProcessComponentBase::GetCycleProgress() const
{
	if (!ActiveProcessTag.IsValid() || ActiveCycleSeconds <= 0.0)
	{
		return 0.0f;
	}
	const double Now = GetServerTimeSeconds();
	const double Elapsed = Now - ServerCycleStartTime;
	return static_cast<float>(FMath::Clamp(Elapsed / ActiveCycleSeconds, 0.0, 1.0));
}

void USimEco_ProcessComponentBase::BroadcastTickCompleted()
{
	if (UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		// Payload: the process tag that just completed a cycle.
		FInstancedStruct Payload;
		FSeam_NetValue Value = FSeam_NetValue::MakeTag(ActiveProcessTag);
		Value.ToInstancedStruct(Payload);
		Bus->BroadcastPayload(SimEcoNativeTags::Bus_TickCompleted, Payload, GetOwner());
	}
}

void USimEco_ProcessComponentBase::OnRep_ActiveProcessTag()
{
	// Clients (and the server, called manually) observe a start/stop here. Subclasses can extend by
	// overriding; the base just logs. No replicated state is mutated.
	UE_LOG(LogDP, Verbose, TEXT("[SimEco_Process] Active process is now %s on %s"),
		ActiveProcessTag.IsValid() ? *ActiveProcessTag.ToString() : TEXT("<idle>"),
		*GetNameSafe(GetOwner()));
}
