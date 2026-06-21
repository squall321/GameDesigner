// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Move_StaminaComponent.h"
#include "Data/Move_StaminaProfile.h"
#include "Settings/Move_DeveloperSettings.h"
#include "Move_NativeTags.h"

#include "Clock/Seam_SimClock.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

#include "Net/UnrealNetwork.h"
#include "Engine/World.h"

UMove_StaminaComponent::UMove_StaminaComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UMove_StaminaComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UMove_StaminaComponent, Stamina);
	DOREPLIFETIME(UMove_StaminaComponent, bExhausted);
}

void UMove_StaminaComponent::BeginPlay()
{
	Super::BeginPlay();

	// Start full on authority; clients receive the value via replication.
	if (GetOwnerRole() == ROLE_Authority)
	{
		Stamina = GetMaxStamina();
		bExhausted = false;
	}

	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	if (Settings == nullptr || Settings->bRegisterStaminaAsService)
	{
		RegisterAsService(true);
	}
}

void UMove_StaminaComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	RegisterAsService(false);
	Super::EndPlay(EndPlayReason);
}

void UMove_StaminaComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Regen is authoritative; clients only mirror the replicated value.
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Honor the shared sim clock: skip while paused, scale by time scale when one is resolvable.
	double TimeScale = 1.0;
	if (TScriptInterface<ISeam_SimClock> Clock = ResolveSimClock())
	{
		if (ISeam_SimClock::Execute_IsPaused(Clock.GetObject()))
		{
			bDrainedThisTick = false;
			return;
		}
		TimeScale = ISeam_SimClock::Execute_GetTimeScale(Clock.GetObject());
	}
	const float ScaledDelta = DeltaTime * static_cast<float>(TimeScale);
	const float Now = World->GetTimeSeconds();

	if (bDrainedThisTick)
	{
		// Drained this tick: push the regen window out and skip regen.
		RegenAllowedTime = Now + GetRegenDelay();
		bDrainedThisTick = false;
		return;
	}

	if (Now < RegenAllowedTime)
	{
		return;
	}

	const float Max = GetMaxStamina();
	if (Stamina < Max)
	{
		const float Regen = GetRegenPerSecond() * ScaledDelta;
		SetStaminaAuthoritative(Stamina + Regen);
	}
}

// ---- ISeam_NeedProvider ----

float UMove_StaminaComponent::GetNeedNormalized_Implementation(FGameplayTag NeedTag) const
{
	if (NeedTag == MoveNativeTags::Need_Stamina)
	{
		const float Max = GetMaxStamina();
		return Max > 0.f ? FMath::Clamp(Stamina / Max, 0.f, 1.f) : 0.f;
	}
	return 0.f;
}

bool UMove_StaminaComponent::SupportsNeed_Implementation(FGameplayTag NeedTag) const
{
	return NeedTag == MoveNativeTags::Need_Stamina;
}

void UMove_StaminaComponent::GetSupportedNeeds_Implementation(FGameplayTagContainer& OutNeeds) const
{
	OutNeeds.AddTag(MoveNativeTags::Need_Stamina);
}

// ---- Queries ----

float UMove_StaminaComponent::GetMaxStamina() const
{
	if (Profile && Profile->MaxStamina > 0.f)
	{
		return Profile->MaxStamina;
	}
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	return Settings ? Settings->FallbackMaxStamina : 100.f;
}

float UMove_StaminaComponent::GetDashCost() const
{
	if (Profile && Profile->DashCost > 0.f)
	{
		return Profile->DashCost;
	}
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	return Settings ? Settings->FallbackDashStaminaCost : 25.f;
}

float UMove_StaminaComponent::GetRegenPerSecond() const
{
	if (Profile && Profile->RegenPerSecond > 0.f)
	{
		return Profile->RegenPerSecond;
	}
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	return Settings ? Settings->FallbackStaminaRegenPerSecond : 15.f;
}

float UMove_StaminaComponent::GetRegenDelay() const
{
	if (Profile && Profile->RegenDelay > 0.f)
	{
		return Profile->RegenDelay;
	}
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	return Settings ? Settings->FallbackRegenDelay : 1.f;
}

float UMove_StaminaComponent::GetExhaustionRecoveryThreshold() const
{
	const float Threshold = Profile ? Profile->ExhaustionRecoveryThreshold : 0.f;
	return FMath::Clamp(Threshold, 0.f, GetMaxStamina());
}

// ---- Authoritative mutators ----

bool UMove_StaminaComponent::TryDrain(float Amount)
{
	// HARD RULE 4: authority guard at the TOP of every mutator of replicated state.
	if (GetOwnerRole() != ROLE_Authority)
	{
		return false;
	}
	if (Amount <= 0.f)
	{
		return true; // a zero/negative drain trivially succeeds without touching state.
	}
	if (Stamina < Amount)
	{
		return false;
	}
	SetStaminaAuthoritative(Stamina - Amount);
	bDrainedThisTick = true;
	return true;
}

void UMove_StaminaComponent::Restore(float Amount)
{
	if (GetOwnerRole() != ROLE_Authority || Amount <= 0.f)
	{
		return;
	}
	SetStaminaAuthoritative(Stamina + Amount);
}

void UMove_StaminaComponent::NotifyDraining()
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}
	bDrainedThisTick = true;
}

void UMove_StaminaComponent::SetStaminaAuthoritative(float NewValue)
{
	const float Max = GetMaxStamina();
	const float Clamped = FMath::Clamp(NewValue, 0.f, Max);
	const bool bWasAboveZero = Stamina > 0.f;

	Stamina = Clamped;

	// Exhaustion lockout: latch when we hit zero, release once recovered past the threshold.
	if (Stamina <= 0.f)
	{
		bExhausted = true;
	}
	else if (bExhausted && Stamina >= GetExhaustionRecoveryThreshold())
	{
		bExhausted = false;
	}

	// Authority fires its own local delegate immediately (clients fire from OnRep).
	OnStaminaChanged.Broadcast(Stamina, Max);
	if (bWasAboveZero && Stamina <= 0.f)
	{
		OnStaminaDepleted.Broadcast();
	}
}

// ---- Replication callback ----

void UMove_StaminaComponent::OnRep_Stamina()
{
	const float Max = GetMaxStamina();
	OnStaminaChanged.Broadcast(Stamina, Max);
	if (Stamina <= 0.f)
	{
		OnStaminaDepleted.Broadcast();
	}
}

// ---- Service & clock resolution ----

TScriptInterface<ISeam_SimClock> UMove_StaminaComponent::ResolveSimClock()
{
	// Re-resolve when the cached object is gone.
	if (UObject* Cached = CachedClockObject.Get())
	{
		TScriptInterface<ISeam_SimClock> Result;
		Result.SetObject(Cached);
		Result.SetInterface(Cast<ISeam_SimClock>(Cached));
		if (Result.GetInterface())
		{
			return Result;
		}
	}

	if (UDP_ServiceLocatorSubsystem* Locator =
			FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// The clock is registered under the shared service key by whatever owns the authoritative clock.
		static const FGameplayTag ClockKey = FGameplayTag::RequestGameplayTag(FName("DP.Service.SimClock"), /*ErrorIfNotFound*/false);
		if (ClockKey.IsValid())
		{
			if (UObject* Provider = Locator->ResolveService(ClockKey))
			{
				if (ISeam_SimClock* AsClock = Cast<ISeam_SimClock>(Provider))
				{
					CachedClockObject = Provider;
					TScriptInterface<ISeam_SimClock> Result;
					Result.SetObject(Provider);
					Result.SetInterface(AsClock);
					return Result;
				}
			}
		}
	}
	return TScriptInterface<ISeam_SimClock>();
}

void UMove_StaminaComponent::RegisterAsService(bool bRegister)
{
	UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return;
	}
	const FGameplayTag Key = MoveNativeTags::Service_Move_Stamina;
	if (bRegister)
	{
		// WeakObserved: a GI-scoped locator must not strong-hold a world-lifetime component.
		Locator->RegisterService(Key, this, EDP_ServiceLifetime::WeakObserved, /*bAllowOverride*/true);
	}
	else if (Locator->IsRegistered(Key))
	{
		Locator->UnregisterService(Key);
	}
}
