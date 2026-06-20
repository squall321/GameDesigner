// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Spawn/Lvl_EncounterActivatorComponent.h"
#include "DesignPatternsLevelDirectorNativeTags.h"
#include "Lvl_BusPayloads.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Activation/Seam_ActivationGate.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject on 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

ULvl_EncounterActivatorComponent::ULvl_EncounterActivatorComponent()
{
	// Cheap polling: tick is enabled but the actual proximity check is throttled to PollInterval.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	// Local/per-machine activation; never replicated.
	SetIsReplicatedByDefault(false);
}

void ULvl_EncounterActivatorComponent::BeginPlay()
{
	Super::BeginPlay();
	// Stagger the first poll by a fraction of the interval (per-owner) so many activators do not all
	// poll on the same frame. Derived from the owner name hash, deterministic and side-effect-free.
	const uint32 Hash = GetOwner() ? GetTypeHash(GetOwner()->GetName()) : 0u;
	PollAccumulator = (PollInterval > 0.f) ? (PollInterval * (static_cast<float>(Hash % 1000u) / 1000.f)) : 0.f;
}

void ULvl_EncounterActivatorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bActive)
	{
		// Make sure a teardown notifies listeners the encounter is no longer active.
		SetActive(false);
	}
	Super::EndPlay(EndPlayReason);
}

void ULvl_EncounterActivatorComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const float Interval = FMath::Max(0.02f, PollInterval);
	PollAccumulator += DeltaTime;
	if (PollAccumulator < Interval)
	{
		return;
	}
	PollAccumulator = 0.f;
	EvaluateActivation();
}

void ULvl_EncounterActivatorComponent::Reevaluate()
{
	PollAccumulator = 0.f;
	EvaluateActivation();
}

UDP_ServiceLocatorSubsystem* ULvl_EncounterActivatorComponent::GetLocator() const
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
}

bool ULvl_EncounterActivatorComponent::IsGateOpen() const
{
	if (!GateKey.IsValid())
	{
		return true; // ungated
	}
	UDP_ServiceLocatorSubsystem* Locator = GetLocator();
	if (!Locator)
	{
		return true; // documented inert default
	}
	UObject* GateObj = Locator->ResolveService(LvlNativeTags::Service_Lvl_ActivationGate);
	if (!GateObj || !GateObj->GetClass()->ImplementsInterface(USeam_ActivationGate::StaticClass()))
	{
		return true; // gate seam unresolved -> open
	}
	return ISeam_ActivationGate::Execute_IsGateOpen(GateObj, GateKey);
}

float ULvl_EncounterActivatorComponent::DistanceToNearestInterest() const
{
	const AActor* Owner = GetOwner();
	const UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return TNumericLimits<float>::Max();
	}
	const FVector Origin = Owner->GetActorLocation();

	float NearestSq = TNumericLimits<float>::Max();

	// Interested actors are the local player pawns (the machine's own viewers). This is intentionally
	// LOCAL: each machine activates against the things it is rendering, which is exactly the right
	// signal for streaming/cosmetic reveal. Authoritative spawners gate on authority separately.
	const UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return NearestSq;
	}

	for (const ULocalPlayer* LocalPlayer : GameInstance->GetLocalPlayers())
	{
		if (!LocalPlayer)
		{
			continue;
		}
		const APlayerController* PC = LocalPlayer->GetPlayerController(World);
		const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		if (!Pawn)
		{
			continue;
		}
		const float DistSq = FVector::DistSquared(Origin, Pawn->GetActorLocation());
		NearestSq = FMath::Min(NearestSq, DistSq);
	}

	return (NearestSq == TNumericLimits<float>::Max()) ? NearestSq : FMath::Sqrt(NearestSq);
}

void ULvl_EncounterActivatorComponent::EvaluateActivation()
{
	const bool bGate = IsGateOpen();

	// Gate closed: deactivate (if allowed) and stop — proximity is irrelevant while gated off.
	if (!bGate)
	{
		if (bActive && bAllowDeactivation)
		{
			SetActive(false);
		}
		return;
	}

	const float Distance = DistanceToNearestInterest();
	const float ActivateAt = FMath::Max(0.f, ActivationRadius);
	const float DeactivateAt = FMath::Max(ActivateAt, DeactivationRadius); // hysteresis: never < activate

	if (!bActive)
	{
		if (Distance <= ActivateAt)
		{
			SetActive(true);
		}
	}
	else
	{
		if (bAllowDeactivation && Distance > DeactivateAt)
		{
			SetActive(false);
		}
	}
}

void ULvl_EncounterActivatorComponent::SetActive(bool bNewActive)
{
	if (bActive == bNewActive)
	{
		return;
	}
	bActive = bNewActive;

	OnEncounterActiveChanged.Broadcast(bActive);
	BroadcastEncounterEvent(bActive);

	UE_LOG(LogDP, Verbose, TEXT("Lvl Encounter (%s region %s): %s."),
		*GetNameSafe(GetOwner()), *RegionTag.ToString(), bActive ? TEXT("ACTIVATED") : TEXT("deactivated"));
}

void ULvl_EncounterActivatorComponent::BroadcastEncounterEvent(bool bNowActive) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}
	FLvl_EncounterEventPayload Payload;
	Payload.RegionTag = RegionTag;
	Payload.GateKey = GateKey;
	Payload.bActive = bNowActive;

	const FGameplayTag Channel = bNowActive
		? LvlNativeTags::Bus_Lvl_Encounter_Activated
		: LvlNativeTags::Bus_Lvl_Encounter_Deactivated;
	Bus->BroadcastPayload(Channel, FInstancedStruct::Make(Payload),
		const_cast<ULvl_EncounterActivatorComponent*>(this));
}
