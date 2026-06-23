// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Prediction/UNet_PredictedStateComponent.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UNet_PredictedStateComponent::UNet_PredictedStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UNet_PredictedStateComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Only the owning client needs to reconcile, so the snapshot is OwnerOnly — simulated proxies never
	// see another player's prediction stream.
	DOREPLIFETIME_CONDITION(UNet_PredictedStateComponent, Snapshot, COND_OwnerOnly);
}

bool UNet_PredictedStateComponent::IsPredictingOwner() const
{
	const AActor* Owner = GetOwner();
	// Predicting requires: this machine owns the actor's input (autonomous proxy) AND the owner has a net
	// connection (so the server RPC actually has a path). On a standalone/listen host the owner has
	// authority and we take the server path instead (handled by callers).
	return Owner && UNet_NetUtilsLibrary::IsAutonomousProxy(Owner);
}

FSeam_NetValue UNet_PredictedStateComponent::SimulateStep_Implementation(const FSeam_NetValue& InState, const FSeam_NetValue& /*Input*/, float /*DeltaTime*/) const
{
	// Identity default: an unconfigured component is inert (state never changes) rather than crashing.
	// Games override this with their deterministic, side-effect-free integration step.
	return InState;
}

bool UNet_PredictedStateComponent::ValidateServerInput_Implementation(const FNet_PredictedInput& /*Input*/) const
{
	return true;
}

void UNet_PredictedStateComponent::SetPredictedState(const FSeam_NetValue& NewState)
{
	if (PredictedState != NewState)
	{
		PredictedState = NewState;
		OnPredictedStateChanged.Broadcast(PredictedState);
	}
}

void UNet_PredictedStateComponent::ApplyLocalStep(const FNet_PredictedInput& Input)
{
	const FSeam_NetValue Next = SimulateStep(PredictedState, Input.Payload, Input.DeltaTime);
	SetPredictedState(Next);
}

void UNet_PredictedStateComponent::SubmitInput(const FSeam_NetValue& Payload, float DeltaTime)
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// SERVER / standalone host path: the owner already has authority — apply authoritatively and publish
	// the snapshot directly (no client RPC). A listen-server's own pawn takes this branch.
	if (UNet_NetUtilsLibrary::HasAuthority(Owner))
	{
		FNet_PredictedInput Input(NextSequence++, DeltaTime, Payload);
		if (!ValidateServerInput(Input))
		{
			UE_LOG(LogDP, Verbose, TEXT("%s: server-side input %u rejected by ValidateServerInput."),
				*GetName(), Input.Sequence);
			return;
		}
		ApplyLocalStep(Input);

		LastAckedSequence = Input.Sequence;
		Snapshot.AckedSequence = LastAckedSequence;
		Snapshot.State = PredictedState;
		Snapshot.ServerTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		// Push-model dirty mark so OnRep_Snapshot fires on the owning client (matches UNet_AuthorityComponent).
		MARK_PROPERTY_DIRTY_FROM_NAME(UNet_PredictedStateComponent, Snapshot, this);
		return;
	}

	// CLIENT prediction path — only valid for the net-owning autonomous proxy.
	if (!IsPredictingOwner())
	{
		if (!bLoggedOwnershipFallback)
		{
			bLoggedOwnershipFallback = true;
			UE_LOG(LogDP, Warning,
				TEXT("%s: SubmitInput called on a non-owning/simulated context (%s). Prediction disabled; "
					 "state will follow server snapshots only."),
				*GetName(), *UNet_NetUtilsLibrary::DescribeNetContext(Owner));
		}
		return;
	}

	// 1) Stamp + apply locally for instant feedback.
	FNet_PredictedInput Input(NextSequence++, DeltaTime, Payload);
	ApplyLocalStep(Input);

	// 2) Buffer for reconciliation, bounding the ring (drop oldest if the client outruns the server).
	PendingInputs.Add(Input);
	while (PendingInputs.Num() > FMath::Max(8, MaxPendingInputs))
	{
		PendingInputs.RemoveAt(0, 1);
	}

	// 3) Forward to the server.
	ServerSubmitInput(Input);
}

bool UNet_PredictedStateComponent::ServerSubmitInput_Validate(FNet_PredictedInput Input)
{
	// Cheap anti-cheat: a finite, bounded delta-time and a set payload. Detailed checks live in the impl.
	const bool bDtSane = FMath::IsFinite(Input.DeltaTime) && Input.DeltaTime >= 0.f && Input.DeltaTime < 5.f;
	return bDtSane && Input.Payload.IsSet();
}

void UNet_PredictedStateComponent::ServerSubmitInput_Implementation(FNet_PredictedInput Input)
{
	if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UNet_PredictedStateComponent::ServerSubmitInput")))
	{
		return;
	}

	// Enforce monotonic sequence: a replayed/old/out-of-order input is dropped (idempotent, anti-cheat).
	if (Input.Sequence <= LastAckedSequence)
	{
		UE_LOG(LogDP, Verbose, TEXT("%s: dropped non-monotonic input seq=%u (last acked=%u)."),
			*GetName(), Input.Sequence, LastAckedSequence);
		return;
	}

	// Game-defined payload validation (re-derive / bound-check on the server — never trust the client).
	if (!ValidateServerInput(Input))
	{
		UE_LOG(LogDP, Verbose, TEXT("%s: input seq=%u failed ValidateServerInput on server."),
			*GetName(), Input.Sequence);
		return;
	}

	// Apply authoritatively and advance the reconciliation snapshot.
	ApplyLocalStep(Input);
	LastAckedSequence = Input.Sequence;

	Snapshot.AckedSequence = LastAckedSequence;
	Snapshot.State = PredictedState;
	Snapshot.ServerTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	// Push-model dirty mark so OnRep_Snapshot fires on the owning client (matches UNet_AuthorityComponent).
	MARK_PROPERTY_DIRTY_FROM_NAME(UNet_PredictedStateComponent, Snapshot, this);
}

bool UNet_PredictedStateComponent::StatesMatch(const FSeam_NetValue& A, const FSeam_NetValue& B) const
{
	if (A.Type != B.Type)
	{
		return false;
	}

	const double Tol = (double)FMath::Max(0.f, ReconcileTolerance);
	switch (A.Type)
	{
	case ESeam_NetValueType::Float:
		return FMath::Abs(A.FloatValue - B.FloatValue) <= Tol;
	case ESeam_NetValueType::Int:
		return A.IntValue == B.IntValue;
	case ESeam_NetValueType::Vector:
		return A.VectorValue.Equals(B.VectorValue, Tol);
	default:
		// Bool / Tag / Name / None: exact match.
		return A == B;
	}
}

void UNet_PredictedStateComponent::OnRep_Snapshot()
{
	// Runs on the owning client when the server pushes a new reconciliation snapshot.
	LastAckedSequence = Snapshot.AckedSequence;

	// 1) Discard every buffered input the server has already accounted for.
	PendingInputs.RemoveAll([this](const FNet_PredictedInput& In)
	{
		return In.Sequence <= Snapshot.AckedSequence;
	});

	// 2) If we are the predicting owner, reconcile against the authoritative state. We rebuild the local
	//    prediction from the server state forward through the still-unacked inputs and compare the result
	//    to what we currently show; if they diverge we adopt the rebuilt state (a "correction").
	bool bCorrected = false;
	if (IsPredictingOwner())
	{
		FSeam_NetValue Rebuilt = Snapshot.State;
		for (const FNet_PredictedInput& In : PendingInputs)
		{
			Rebuilt = SimulateStep(Rebuilt, In.Payload, In.DeltaTime);
		}

		if (!StatesMatch(Rebuilt, PredictedState))
		{
			bCorrected = true;
			SetPredictedState(Rebuilt);
		}
	}
	else
	{
		// Simulated/non-owning: just follow the server state directly.
		SetPredictedState(Snapshot.State);
	}

	OnReconciled.Broadcast(bCorrected);

	if (bCorrected)
	{
		UE_LOG(LogDP, Verbose, TEXT("%s: reconciled correction at acked seq=%u (%d inputs replayed)."),
			*GetName(), Snapshot.AckedSequence, PendingInputs.Num());
	}
}
