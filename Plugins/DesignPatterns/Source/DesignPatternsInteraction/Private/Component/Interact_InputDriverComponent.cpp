// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Interact_InputDriverComponent.h"
#include "Component/Interact_InteractorComponent.h"
#include "Data/Interact_VerbDefinition.h"
#include "Data/Interact_VerbDefinitionEx.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Data/DPDataRegistrySubsystem.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

UInteract_InputDriverComponent::UInteract_InputDriverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false);
}

void UInteract_InputDriverComponent::BeginPlay()
{
	Super::BeginPlay();
	Interactor = ResolveInteractor();
}

void UInteract_InputDriverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// If we are mid-hold when torn down, request a clean cancel so the interactable is not left holding.
	if (bInputHeld && ActiveVerb.IsValid())
	{
		if (UInteract_InteractorComponent* Inter = ResolveInteractor())
		{
			if (Inter->IsInteracting())
			{
				Inter->RequestEndInteract(EInteract_EndReason::Cancelled);
			}
		}
	}
	Super::EndPlay(EndPlayReason);
}

UInteract_InteractorComponent* UInteract_InputDriverComponent::ResolveInteractor() const
{
	if (Interactor.IsValid())
	{
		return Interactor.Get();
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<UInteract_InteractorComponent>();
	}
	return nullptr;
}

const UInteract_VerbDefinition* UInteract_InputDriverComponent::ResolveFocusVerbDef(FGameplayTag& OutVerb) const
{
	OutVerb = FGameplayTag();

	UInteract_InteractorComponent* Inter = const_cast<UInteract_InputDriverComponent*>(this)->ResolveInteractor();
	if (!Inter)
	{
		return nullptr;
	}

	// Build the focus verb menu and use its default verb (folds availability), or the override.
	FInteract_VerbMenu Menu;
	Inter->GetFocusVerbMenu(Menu);

	if (DefaultVerbOverride.IsValid())
	{
		OutVerb = DefaultVerbOverride;
	}
	else if (Menu.Verbs.IsValidIndex(Menu.DefaultVerbIndex))
	{
		OutVerb = Menu.Verbs[Menu.DefaultVerbIndex].Verb;
	}

	if (!OutVerb.IsValid())
	{
		return nullptr;
	}

	if (UDP_DataRegistrySubsystem* Registry = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_DataRegistrySubsystem>(this))
	{
		return Cast<UInteract_VerbDefinition>(Registry->FindByTag(OutVerb));
	}
	return nullptr;
}

void UInteract_InputDriverComponent::BeginGesture()
{
	FGameplayTag Verb;
	ResolveFocusVerbDef(Verb); // def used per-tick; capture verb for the gesture
	ActiveVerb = Verb;

	const UWorld* World = GetWorld();
	PressStartTime = World ? World->GetTimeSeconds() : 0.0;
	LocalChargeAlpha = 0.f;
	LocalHoldAlpha = 0.f;
	bGestureFired = false;
	RepeatAccumulator = 0.f;
}

void UInteract_InputDriverComponent::FireInteract()
{
	if (UInteract_InteractorComponent* Inter = ResolveInteractor())
	{
		Inter->RequestInteract(ActiveVerb);
	}
}

void UInteract_InputDriverComponent::NotifyInteractPressed()
{
	bInputHeld = true;
	BeginGesture();

	FGameplayTag Verb;
	const UInteract_VerbDefinition* Def = ResolveFocusVerbDef(Verb);
	const EInteract_ActivationMode Mode = UInteract_VerbDefinitionEx::ResolveActivationMode(Def);

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;

	switch (Mode)
	{
	case EInteract_ActivationMode::Tap:
		FireInteract();
		bGestureFired = true;
		break;

	case EInteract_ActivationMode::Hold:
		// Start the hold on the interactor; the server measures completion against the replicated start.
		FireInteract();
		break;

	case EInteract_ActivationMode::Charge:
		// Charge accumulates per-tick; fires on release.
		break;

	case EInteract_ActivationMode::DoubleTap:
	{
		const float Window = (Def && Def->IsA<UInteract_VerbDefinitionEx>())
			? Cast<UInteract_VerbDefinitionEx>(Def)->DoubleTapWindowSeconds : 0.3f;
		if (LastTapTime >= 0.0 && (Now - LastTapTime) <= Window)
		{
			FireInteract();
			bGestureFired = true;
			LastTapTime = -1.0;
		}
		else
		{
			LastTapTime = Now;
		}
		break;
	}

	case EInteract_ActivationMode::Repeat:
		// First fire immediately; subsequent fires are paced in TickComponent while held.
		FireInteract();
		RepeatAccumulator = 0.f;
		break;

	default:
		break;
	}
}

void UInteract_InputDriverComponent::NotifyInteractReleased()
{
	if (!bInputHeld)
	{
		return;
	}
	bInputHeld = false;

	FGameplayTag Verb;
	const UInteract_VerbDefinition* Def = ResolveFocusVerbDef(Verb);
	const EInteract_ActivationMode Mode = UInteract_VerbDefinitionEx::ResolveActivationMode(Def);

	switch (Mode)
	{
	case EInteract_ActivationMode::Hold:
		// Released before the server confirmed completion ⇒ cancel the in-progress hold.
		if (UInteract_InteractorComponent* Inter = ResolveInteractor())
		{
			if (Inter->IsInteracting() && Inter->GetActiveHoldProgress() < 1.f)
			{
				Inter->RequestEndInteract(EInteract_EndReason::Cancelled);
			}
		}
		break;

	case EInteract_ActivationMode::Charge:
		// Fire on release with whatever charge was accumulated (the interactor/server interprets it
		// through the verb; the local alpha is reported via OnLocalProgress for UI).
		if (!bGestureFired)
		{
			FireInteract();
			bGestureFired = true;
		}
		break;

	default:
		break;
	}

	LocalChargeAlpha = 0.f;
	LocalHoldAlpha = 0.f;
}

void UInteract_InputDriverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bInputHeld)
	{
		return;
	}

	FGameplayTag Verb;
	const UInteract_VerbDefinition* Def = ResolveFocusVerbDef(Verb);
	const EInteract_ActivationMode Mode = UInteract_VerbDefinitionEx::ResolveActivationMode(Def);

	switch (Mode)
	{
	case EInteract_ActivationMode::Hold:
	{
		// Mirror the interactor's authoritative hold progress locally for the meter.
		if (UInteract_InteractorComponent* Inter = ResolveInteractor())
		{
			LocalHoldAlpha = Inter->GetActiveHoldProgress();
			OnLocalProgress.Broadcast(ActiveVerb, LocalHoldAlpha);
		}
		break;
	}

	case EInteract_ActivationMode::Charge:
	{
		const float ChargeMax = (Def && Def->IsA<UInteract_VerbDefinitionEx>())
			? FMath::Max(KINDA_SMALL_NUMBER, Cast<UInteract_VerbDefinitionEx>(Def)->ChargeMaxSeconds)
			: 1.f;
		const UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : PressStartTime;
		LocalChargeAlpha = FMath::Clamp(static_cast<float>((Now - PressStartTime) / ChargeMax), 0.f, 1.f);
		OnLocalProgress.Broadcast(ActiveVerb, LocalChargeAlpha);
		break;
	}

	case EInteract_ActivationMode::Repeat:
	{
		const float Interval = (Def && Def->IsA<UInteract_VerbDefinitionEx>())
			? FMath::Max(0.01f, Cast<UInteract_VerbDefinitionEx>(Def)->RepeatIntervalSeconds)
			: 0.25f;
		RepeatAccumulator += DeltaTime;
		while (RepeatAccumulator >= Interval)
		{
			RepeatAccumulator -= Interval;
			FireInteract();
		}
		break;
	}

	default:
		break;
	}
}

float UInteract_InputDriverComponent::GetLocalActivationAlpha() const
{
	// Whichever meter is active dominates; both are 0 when idle.
	return FMath::Max(LocalHoldAlpha, LocalChargeAlpha);
}
