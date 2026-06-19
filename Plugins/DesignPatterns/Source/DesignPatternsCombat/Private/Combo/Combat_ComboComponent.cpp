// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Combo/Combat_ComboComponent.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UCombat_ComboComponent::UCombat_ComboComponent()
{
	// Tick is needed only to detect the chain window lapsing back to idle.
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

float UCombat_ComboComponent::GetWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

FGameplayTag UCombat_ComboComponent::GetCurrentStepTag() const
{
	if (Steps.IsValidIndex(CurrentStepIndex))
	{
		return Steps[CurrentStepIndex].StepTag;
	}
	return FGameplayTag();
}

void UCombat_ComboComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// If a chain is active and its window has lapsed, drop back to idle.
	if (CurrentStepIndex != INDEX_NONE && GetWorldTimeSeconds() > WindowDeadline)
	{
		ResetCombo();
	}
}

FGameplayTag UCombat_ComboComponent::PushInput()
{
	if (Steps.Num() == 0)
	{
		return FGameplayTag();
	}

	const float Now = GetWorldTimeSeconds();
	const bool bWindowOpen = (CurrentStepIndex != INDEX_NONE) && (Now <= WindowDeadline);

	if (!bWindowOpen)
	{
		// Idle or window lapsed: start a fresh combo at the opener.
		EnterStep(0);
	}
	else
	{
		const int32 NextIndex = CurrentStepIndex + 1;
		if (Steps.IsValidIndex(NextIndex))
		{
			EnterStep(NextIndex);
		}
		else if (bLoopAtEnd)
		{
			EnterStep(0);
		}
		else
		{
			// Past the final step: end this chain and immediately open a new one.
			ResetCombo();
			EnterStep(0);
		}
	}

	return GetCurrentStepTag();
}

void UCombat_ComboComponent::EnterStep(int32 Index)
{
	if (!Steps.IsValidIndex(Index))
	{
		return;
	}

	CurrentStepIndex = Index;
	const FCombat_ComboStep& Step = Steps[Index];
	WindowDeadline = GetWorldTimeSeconds() + FMath::Max(0.f, Step.ChainWindow);

	OnComboAdvanced.Broadcast(this, Step.StepTag, Index);

	UE_LOG(LogDP, Verbose, TEXT("[Combat] %s combo -> step %d (%s)"),
		*GetNameSafe(GetOwner()), Index, *Step.StepTag.ToString());
}

void UCombat_ComboComponent::ResetCombo()
{
	if (CurrentStepIndex == INDEX_NONE)
	{
		return;
	}

	CurrentStepIndex = INDEX_NONE;
	WindowDeadline = 0.f;
	OnComboReset.Broadcast(this);
}
