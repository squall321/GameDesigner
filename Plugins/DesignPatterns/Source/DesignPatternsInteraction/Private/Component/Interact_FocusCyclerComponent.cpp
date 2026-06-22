// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Component/Interact_FocusCyclerComponent.h"
#include "Component/Interact_InteractorComponent.h"

#include "Core/DPLog.h"
#include "GameFramework/Actor.h"

UInteract_FocusCyclerComponent::UInteract_FocusCyclerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(false);
}

void UInteract_FocusCyclerComponent::BeginPlay()
{
	Super::BeginPlay();
	Interactor = ResolveInteractor();
}

void UInteract_FocusCyclerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Release any local override we installed so we do not leave the interactor pinned to a target.
	if (CycledActor.IsValid())
	{
		if (UInteract_InteractorComponent* Inter = ResolveInteractor())
		{
			Inter->ClearLocalFocusOverride();
		}
	}
	Super::EndPlay(EndPlayReason);
}

UInteract_InteractorComponent* UInteract_FocusCyclerComponent::ResolveInteractor() const
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

void UInteract_FocusCyclerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	RefreshAccumulator += DeltaTime;
	const float Interval = 1.f / FMath::Max(1.f, RefreshHz);
	if (RefreshAccumulator < Interval)
	{
		return;
	}
	RefreshAccumulator = 0.f;

	if (RefreshCandidates())
	{
		TArray<AActor*> AsActors;
		AsActors.Reserve(CachedCandidates.Num());
		for (const TWeakObjectPtr<AActor>& Weak : CachedCandidates)
		{
			if (AActor* Actor = Weak.Get())
			{
				AsActors.Add(Actor);
			}
		}
		OnCandidatesChanged.Broadcast(AsActors);

		// If our cycled target left the candidate set, drop the override.
		if (CycledActor.IsValid() && !CachedCandidates.Contains(CycledActor))
		{
			ResetToStrategyPick();
		}
	}
}

bool UInteract_FocusCyclerComponent::RefreshCandidates()
{
	UInteract_InteractorComponent* Inter = ResolveInteractor();
	if (!Inter)
	{
		const bool bChanged = CachedCandidates.Num() > 0;
		CachedCandidates.Reset();
		return bChanged;
	}

	TArray<AActor*> Current;
	Inter->GetLocalCandidateActors(Current);

	// Detect a change against the cached weak list.
	bool bChanged = Current.Num() != CachedCandidates.Num();
	if (!bChanged)
	{
		for (int32 Index = 0; Index < Current.Num(); ++Index)
		{
			if (CachedCandidates[Index].Get() != Current[Index])
			{
				bChanged = true;
				break;
			}
		}
	}

	if (bChanged)
	{
		CachedCandidates.Reset(Current.Num());
		for (AActor* Actor : Current)
		{
			CachedCandidates.Add(Actor);
		}
	}
	return bChanged;
}

void UInteract_FocusCyclerComponent::Step(int32 Delta)
{
	RefreshCandidates();

	if (CachedCandidates.Num() == 0)
	{
		return;
	}

	// Find the current index (the cycled actor), defaulting to before-first so +1 lands on index 0.
	int32 CurrentIndex = INDEX_NONE;
	for (int32 Index = 0; Index < CachedCandidates.Num(); ++Index)
	{
		if (CachedCandidates[Index].Get() == CycledActor.Get())
		{
			CurrentIndex = Index;
			break;
		}
	}

	const int32 Count = CachedCandidates.Num();
	const int32 Start = (CurrentIndex == INDEX_NONE) ? (Delta > 0 ? -1 : 0) : CurrentIndex;
	const int32 NextIndex = ((Start + Delta) % Count + Count) % Count;

	AActor* NextActor = CachedCandidates[NextIndex].Get();
	CycledActor = NextActor;

	if (UInteract_InteractorComponent* Inter = ResolveInteractor())
	{
		Inter->SetLocalFocusOverride(NextActor);
	}
}

void UInteract_FocusCyclerComponent::CycleNext()
{
	Step(+1);
}

void UInteract_FocusCyclerComponent::CyclePrev()
{
	Step(-1);
}

void UInteract_FocusCyclerComponent::ResetToStrategyPick()
{
	CycledActor.Reset();
	if (UInteract_InteractorComponent* Inter = ResolveInteractor())
	{
		Inter->ClearLocalFocusOverride();
	}
}

void UInteract_FocusCyclerComponent::InteractWithCycled(FGameplayTag DesiredVerb)
{
	UInteract_InteractorComponent* Inter = ResolveInteractor();
	if (!Inter)
	{
		return;
	}

	AActor* Target = CycledActor.Get();
	if (Target)
	{
		// Route through the server-validated targeted path: the server re-checks reachability.
		Inter->RequestInteractAt(DesiredVerb, Target);
	}
	else
	{
		// No explicit cycled target — fall back to the focus-driven request.
		Inter->RequestInteract(DesiredVerb);
	}
}
