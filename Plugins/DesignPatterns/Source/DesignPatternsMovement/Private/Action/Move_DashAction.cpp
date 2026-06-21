// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Action/Move_DashAction.h"
#include "Component/Move_StaminaComponent.h"
#include "Settings/Move_DeveloperSettings.h"
#include "Move_NativeTags.h"

#include "Action/DPGameplayActionComponent.h"
#include "Needs/Seam_NeedProvider.h"

#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Core/DPLog.h"

UMove_DashAction::UMove_DashAction()
{
	ActionTag = MoveNativeTags::Request_Dash;
	// Cooldown defaults from settings at runtime via the granting component; CooldownDuration may also be
	// authored on the action CDO. We seed it from settings here so a freshly-granted dash has a sane CD.
	if (const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get())
	{
		CooldownDuration = Settings->FallbackDashCooldown;
	}
}

UDP_GameplayActionComponent* UMove_DashAction::ResolveActionComponent(const FDP_ActionActivationData& Data) const
{
	if (UDP_GameplayActionComponent* FromData = Data.SourceComponent.Get())
	{
		return FromData;
	}
	if (const AActor* Owner = Data.Instigator.Get())
	{
		return Owner->FindComponentByClass<UDP_GameplayActionComponent>();
	}
	// Fall back to the action's owning component via the Outer chain helper (protected base method).
	return GetOwningComponent();
}

UMove_StaminaComponent* UMove_DashAction::ResolveStaminaComponent(const FDP_ActionActivationData& Data) const
{
	const AActor* Owner = Data.Instigator.Get();
	if (!Owner)
	{
		if (const UDP_GameplayActionComponent* Comp = GetOwningComponent())
		{
			Owner = Comp->GetOwner();
		}
	}
	return Owner ? Owner->FindComponentByClass<UMove_StaminaComponent>() : nullptr;
}

float UMove_DashAction::ResolveStaminaCost(const FDP_ActionActivationData& Data) const
{
	if (StaminaCostOverride > 0.f)
	{
		return StaminaCostOverride;
	}
	if (const UMove_StaminaComponent* Stamina = ResolveStaminaComponent(Data))
	{
		return Stamina->GetDashCost();
	}
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	return Settings ? Settings->FallbackDashStaminaCost : 25.f;
}

float UMove_DashAction::ResolveDashDuration() const
{
	if (DashDurationOverride > 0.f)
	{
		return DashDurationOverride;
	}
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	return Settings ? Settings->FallbackDashDuration : 0.2f;
}

float UMove_DashAction::ResolveIFrameFraction() const
{
	if (IFrameFractionOverride > 0.f)
	{
		return IFrameFractionOverride;
	}
	const UMove_DeveloperSettings* Settings = UMove_DeveloperSettings::Get();
	return Settings ? Settings->FallbackDashIFrameFraction : 1.f;
}

bool UMove_DashAction::CanActivate_Implementation(const FDP_ActionActivationData& Data) const
{
	// Base tag gating first (required/blocked owned tags).
	if (!Super::CanActivate_Implementation(Data))
	{
		return false;
	}

	// Stamina gate via the shared need seam: read normalized stamina from the provider on the owner.
	if (UMove_StaminaComponent* Stamina = ResolveStaminaComponent(Data))
	{
		if (Stamina->IsExhausted())
		{
			return false;
		}
		const float Cost = ResolveStaminaCost(Data);
		if (!Stamina->HasStamina(Cost))
		{
			return false;
		}
	}
	return true;
}

bool UMove_DashAction::Activate_Implementation(const FDP_ActionActivationData& Data)
{
	UWorld* World = GetWorld();
	UDP_GameplayActionComponent* ActionComp = ResolveActionComponent(Data);
	if (!World || !ActionComp)
	{
		return false;
	}

	// Pay the stamina cost on authority. The stamina component guards authority internally; on a client
	// this is a no-op and the server's drain is what counts (cosmetic prediction may still play).
	if (UMove_StaminaComponent* Stamina = ResolveStaminaComponent(Data))
	{
		Stamina->TryDrain(ResolveStaminaCost(Data));
	}

	// Add the shared i-frame tag (replicated + authority-guarded inside the action component).
	ActionComp->AddOwnedTag(MoveNativeTags::Status_IFrame);

	// Schedule its removal at the end of the i-frame window. The i-frame window is a fraction of the dash.
	const float DashDuration = ResolveDashDuration();
	const float IFrameWindow = FMath::Max(DashDuration * ResolveIFrameFraction(), 0.f);
	if (IFrameWindow > 0.f)
	{
		World->GetTimerManager().SetTimer(
			IFrameTimerHandle,
			FTimerDelegate::CreateUObject(this, &UMove_DashAction::RemoveIFrameTag),
			IFrameWindow, /*bLoop*/false);
	}
	else
	{
		// Degenerate window: remove immediately so the tag never lingers.
		RemoveIFrameTag();
	}

	UE_LOG(LogDPAction, Verbose, TEXT("[Movement] Dash activated (cost=%.1f, iframe=%.2fs)."),
		ResolveStaminaCost(Data), IFrameWindow);
	return true;
}

void UMove_DashAction::EndAction_Implementation(const FDP_ActionActivationData& Data, bool bWasCancelled)
{
	// Defensive: force-remove the i-frame tag so it can never leak past the action's lifetime.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(IFrameTimerHandle);
	}
	RemoveIFrameTag();
	Super::EndAction_Implementation(Data, bWasCancelled);
}

void UMove_DashAction::RemoveIFrameTag()
{
	if (UDP_GameplayActionComponent* ActionComp = GetOwningComponent())
	{
		ActionComp->RemoveOwnedTag(MoveNativeTags::Status_IFrame);
	}
}
