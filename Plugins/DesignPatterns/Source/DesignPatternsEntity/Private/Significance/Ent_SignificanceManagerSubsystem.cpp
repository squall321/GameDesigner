// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Significance/Ent_SignificanceManagerSubsystem.h"
#include "Significance/Ent_SignificanceComponent.h"
#include "Significance/Ent_SignificanceSettings.h"

#include "Core/DPLog.h"

#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UEnt_SignificanceManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Components.Reset();
	EnsureTimer();
}

void UEnt_SignificanceManagerSubsystem::Deinitialize()
{
	// Own every timer we create: clear the recompute timer on teardown.
	if (RecomputeTimerHandle.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(RecomputeTimerHandle);
		}
		RecomputeTimerHandle.Invalidate();
	}
	Components.Reset();
	SignificanceSource.Reset();
	Super::Deinitialize();
}

void UEnt_SignificanceManagerSubsystem::EnsureTimer()
{
	UWorld* World = GetWorld();
	if (!World || RecomputeTimerHandle.IsValid())
	{
		return;
	}
	World->GetTimerManager().SetTimer(
		RecomputeTimerHandle, this, &UEnt_SignificanceManagerSubsystem::TimerRecompute,
		FMath::Max(0.05f, UpdatePeriodSeconds), /*bLoop=*/true);
}

void UEnt_SignificanceManagerSubsystem::RegisterSignificanceComponent(UEnt_SignificanceComponent* Component)
{
	if (!Component)
	{
		return;
	}
	const bool bAlready = Components.ContainsByPredicate(
		[Component](const TWeakObjectPtr<UEnt_SignificanceComponent>& W) { return W.Get() == Component; });
	if (!bAlready)
	{
		Components.Add(Component);
	}
	EnsureTimer();
}

void UEnt_SignificanceManagerSubsystem::UnregisterSignificanceComponent(UEnt_SignificanceComponent* Component)
{
	Components.RemoveAll([Component](const TWeakObjectPtr<UEnt_SignificanceComponent>& W)
	{
		return !W.IsValid() || W.Get() == Component;
	});
}

void UEnt_SignificanceManagerSubsystem::SetSignificanceSource(AActor* Source)
{
	SignificanceSource = Source;
}

bool UEnt_SignificanceManagerSubsystem::GetSourceLocation(FVector& OutLocation) const
{
	if (AActor* Source = SignificanceSource.Get())
	{
		OutLocation = Source->GetActorLocation();
		return true;
	}
	// Fall back to the local player's view/pawn.
	if (const UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector ViewLoc; FRotator ViewRot;
			PC->GetPlayerViewPoint(ViewLoc, ViewRot);
			OutLocation = ViewLoc;
			return true;
		}
	}
	return false;
}

void UEnt_SignificanceManagerSubsystem::TimerRecompute()
{
	RecomputeNow();
}

void UEnt_SignificanceManagerSubsystem::RecomputeNow()
{
	// Prune dead entries first.
	Components.RemoveAll([](const TWeakObjectPtr<UEnt_SignificanceComponent>& W) { return !W.IsValid(); });

	FVector SourceLoc;
	const bool bHaveSource = GetSourceLocation(SourceLoc);

	// No bands authored, or no source: everything stays High at default detail/tick.
	const UEnt_SignificanceSettings* Cfg = Settings;
	if (!bHaveSource || !Cfg || Cfg->Buckets.Num() == 0)
	{
		for (const TWeakObjectPtr<UEnt_SignificanceComponent>& W : Components)
		{
			if (UEnt_SignificanceComponent* C = W.Get())
			{
				C->SetBucket(EEnt_SignificanceBucket::High, 0.f, 0);
			}
		}
		return;
	}

	// Score = weighted distance (lower is more significant). ImportanceWeight > 1 pulls an entity nearer.
	struct FScored
	{
		UEnt_SignificanceComponent* Component = nullptr;
		float Distance = 0.f;
		float WeightedDistance = 0.f;
	};
	TArray<FScored> Scored;
	Scored.Reserve(Components.Num());
	for (const TWeakObjectPtr<UEnt_SignificanceComponent>& W : Components)
	{
		UEnt_SignificanceComponent* C = W.Get();
		const AActor* Owner = C ? C->GetOwner() : nullptr;
		if (!C || !Owner)
		{
			continue;
		}
		const float Dist = static_cast<float>(FVector::Dist(Owner->GetActorLocation(), SourceLoc));
		const float Weight = FMath::Max(KINDA_SMALL_NUMBER, C->ImportanceWeight);
		Scored.Add(FScored{ C, Dist, Dist / Weight });
	}

	// Nearest (most significant) first so count budgets fill from the front.
	Scored.Sort([](const FScored& A, const FScored& B) { return A.WeightedDistance < B.WeightedDistance; });

	// Track how many we've assigned per band so we can spill on budget overflow. Bands are authored
	// finest-first; the coarsest band is the catch-all when an entity exceeds all MaxDistances.
	TArray<int32> AssignedPerBand;
	AssignedPerBand.Init(0, Cfg->Buckets.Num());

	const int32 CoarsestIndex = Cfg->Buckets.Num() - 1;

	for (const FScored& S : Scored)
	{
		int32 ChosenBand = CoarsestIndex;

		for (int32 BandIndex = 0; BandIndex < Cfg->Buckets.Num(); ++BandIndex)
		{
			const FEnt_SignificanceBucketDef& Band = Cfg->Buckets[BandIndex];
			if (S.Distance > Band.MaxDistance)
			{
				continue; // Outside this band's distance: try a coarser one.
			}
			const bool bBudgetOk = (Band.CountBudget <= 0) || (AssignedPerBand[BandIndex] < Band.CountBudget);
			if (bBudgetOk)
			{
				ChosenBand = BandIndex;
				break;
			}
			// Within distance but band is full: spill to the next-coarser band (continue the loop).
		}

		++AssignedPerBand[ChosenBand];
		const FEnt_SignificanceBucketDef& Band = Cfg->Buckets[ChosenBand];
		S.Component->SetBucket(Band.Bucket, Band.TickInterval, Band.DetailLevel);
	}
}

FString UEnt_SignificanceManagerSubsystem::GetDPDebugString_Implementation() const
{
	int32 Live = 0;
	for (const TWeakObjectPtr<UEnt_SignificanceComponent>& W : Components)
	{
		if (W.IsValid()) { ++Live; }
	}
	return FString::Printf(TEXT("Significance: %d entities, period=%.2fs, settings=%s"),
		Live, UpdatePeriodSeconds, Settings ? *Settings->GetName() : TEXT("<none>"));
}
