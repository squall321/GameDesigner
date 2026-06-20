// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Facility/SimEco_FacilityComponent.h"
#include "Economy/SimEco_EconomySubsystem.h"
#include "Market/SimEco_MarketSubsystem.h"
#include "Market/SimEco_Market.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

//~ FSimEco_JobEntry replication callbacks (client side) --------------------------------------

void FSimEco_JobEntry::PostReplicatedAdd(const FSimEco_JobArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimEco_JobEntry::PostReplicatedChange(const FSimEco_JobArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

void FSimEco_JobEntry::PreReplicatedRemove(const FSimEco_JobArray& InArraySerializer)
{
	if (InArraySerializer.OwnerComponent)
	{
		InArraySerializer.OwnerComponent->HandleReplicatedChange();
	}
}

//~ USimEco_FacilityComponent -----------------------------------------------------------------

USimEco_FacilityComponent::USimEco_FacilityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);

	// Wire the fast-array back-pointer so entry callbacks can notify us (server and client).
	Jobs.OwnerComponent = this;
}

void USimEco_FacilityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimEco_FacilityComponent, Jobs);
}

void USimEco_FacilityComponent::BeginPlay()
{
	Super::BeginPlay();
	Jobs.OwnerComponent = this;
	RegisterWithDriver();
}

void USimEco_FacilityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnregisterFromDriver();
	Super::EndPlay(EndPlayReason);
}

void USimEco_FacilityComponent::RegisterWithDriver()
{
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority() || bRegisteredWithDriver)
	{
		return;
	}
	if (USimEco_EconomySubsystem* Driver =
		FDP_SubsystemStatics::GetWorldSubsystem<USimEco_EconomySubsystem>(this))
	{
		TScriptInterface<ISimEco_StepListener> Self;
		Self.SetObject(this);
		Self.SetInterface(static_cast<ISimEco_StepListener*>(this));
		Driver->RegisterStepListener(Self);
		bRegisteredWithDriver = true;
	}
}

void USimEco_FacilityComponent::UnregisterFromDriver()
{
	if (!bRegisteredWithDriver)
	{
		return;
	}
	if (USimEco_EconomySubsystem* Driver =
		FDP_SubsystemStatics::GetWorldSubsystem<USimEco_EconomySubsystem>(this))
	{
		TScriptInterface<ISimEco_StepListener> Self;
		Self.SetObject(this);
		Self.SetInterface(static_cast<ISimEco_StepListener*>(this));
		Driver->UnregisterStepListener(Self);
	}
	bRegisteredWithDriver = false;
}

void USimEco_FacilityComponent::MarkJobDirtyAndNotify(FSimEco_JobEntry& Entry)
{
	Jobs.MarkItemDirty(Entry);
	OnQueueChanged.Broadcast(this);
}

void USimEco_FacilityComponent::HandleReplicatedChange()
{
	OnQueueChanged.Broadcast(this);
}

int32 USimEco_FacilityComponent::StartJob(FGameplayTag RecipeTag, int32 StepsRequired)
{
	// AUTHORITY GUARD: never mutate the replicated queue on a client.
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return INDEX_NONE;
	}
	if (!RecipeTag.IsValid())
	{
		return INDEX_NONE;
	}
	if (Jobs.Entries.Num() >= FMath::Max(1, MaxQueueLength))
	{
		UE_LOG(LogDP, Verbose, TEXT("SimEco facility queue full; StartJob(%s) rejected."),
			*RecipeTag.ToString());
		return INDEX_NONE;
	}

	FSimEco_JobEntry& Entry = Jobs.Entries.AddDefaulted_GetRef();
	Entry.JobId = NextJobId++;
	Entry.RecipeTag = RecipeTag;
	Entry.State = ESimEco_JobState::Pending;
	Entry.StartStepIndex = -1;
	Entry.StepsRequired = FMath::Max(1, StepsRequired);
	MarkJobDirtyAndNotify(Entry);

	return Entry.JobId;
}

bool USimEco_FacilityComponent::CancelJob(int32 JobId)
{
	// AUTHORITY GUARD.
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return false;
	}

	const int32 Index = Jobs.Entries.IndexOfByPredicate(
		[JobId](const FSimEco_JobEntry& E) { return E.JobId == JobId; });
	if (Index == INDEX_NONE)
	{
		return false;
	}

	const bool bWasRunning = (Jobs.Entries[Index].State == ESimEco_JobState::Running);
	if (bWasRunning)
	{
		// Return the inputs this job had committed.
		ReleaseReserved();
	}

	Jobs.Entries.RemoveAt(Index);
	Jobs.MarkArrayDirty();
	OnQueueChanged.Broadcast(this);
	return true;
}

float USimEco_FacilityComponent::GetRunningJobProgress() const
{
	const FSimEco_JobEntry* Head = Jobs.Entries.FindByPredicate(
		[](const FSimEco_JobEntry& E) { return E.State == ESimEco_JobState::Running; });
	if (!Head || Head->StepsRequired <= 0 || Head->StartStepIndex < 0)
	{
		return 0.0f;
	}

	// Derive progress from the live driver's step index against the replicated start step.
	int64 CurrentStep = Head->StartStepIndex;
	if (const USimEco_EconomySubsystem* Driver =
		FDP_SubsystemStatics::GetWorldSubsystem<USimEco_EconomySubsystem>(this))
	{
		CurrentStep = Driver->GetStepIndex();
	}
	const int64 Elapsed = FMath::Max<int64>(0, CurrentStep - Head->StartStepIndex);
	return FMath::Clamp(static_cast<float>(Elapsed) / static_cast<float>(Head->StepsRequired), 0.0f, 1.0f);
}

void USimEco_FacilityComponent::TryStartHeadJob(int64 CurrentStepIndex)
{
	// No running job? Promote the first pending job to Running and reserve its inputs.
	const bool bHasRunning = Jobs.Entries.ContainsByPredicate(
		[](const FSimEco_JobEntry& E) { return E.State == ESimEco_JobState::Running; });
	if (bHasRunning)
	{
		return;
	}

	FSimEco_JobEntry* Pending = Jobs.Entries.FindByPredicate(
		[](const FSimEco_JobEntry& E) { return E.State == ESimEco_JobState::Pending; });
	if (!Pending)
	{
		return;
	}

	// Reserve inputs (server-only bookkeeping). In this self-contained model the facility holds the
	// reservation; an integration that wires a stockpile seam would debit it here instead.
	ReservedInputs = DefaultInputs;

	Pending->State = ESimEco_JobState::Running;
	Pending->StartStepIndex = CurrentStepIndex;
	MarkJobDirtyAndNotify(*Pending);
}

void USimEco_FacilityComponent::ReleaseReserved()
{
	// Returning reserved inputs is, in this self-contained model, simply forgetting the reservation.
	// A stockpile-integrated facility would credit the quantities back here.
	ReservedInputs.Reset();
}

void USimEco_FacilityComponent::EmitOutputsToMarket()
{
	if (DefaultOutputs.Num() == 0)
	{
		return;
	}
	USimEco_MarketSubsystem* Market =
		FDP_SubsystemStatics::GetWorldSubsystem<USimEco_MarketSubsystem>(this);
	if (!Market)
	{
		return;
	}

	for (const TPair<FGameplayTag, double>& Output : DefaultOutputs)
	{
		if (!Output.Key.IsValid() || Output.Value <= 0.0)
		{
			continue;
		}
		// Produced goods enter the market as supply (a market sell order). PlaceOrder is authority-
		// guarded internally; we are on the server here.
		FSimEco_Order Order(Output.Key, ESimEco_OrderSide::Sell, Output.Value);
		Market->PlaceOrder_Implementation(Order);
	}
}

void USimEco_FacilityComponent::CompleteHeadJob()
{
	const int32 Index = Jobs.Entries.IndexOfByPredicate(
		[](const FSimEco_JobEntry& E) { return E.State == ESimEco_JobState::Running; });
	if (Index == INDEX_NONE)
	{
		return;
	}

	const FGameplayTag RecipeTag = Jobs.Entries[Index].RecipeTag;

	// Emit outputs (consumes the reservation conceptually) then clear the reservation.
	EmitOutputsToMarket();
	ReservedInputs.Reset();

	// Dequeue the finished job.
	Jobs.Entries.RemoveAt(Index);
	Jobs.MarkArrayDirty();
	OnQueueChanged.Broadcast(this);

	OnJobCompleted.Broadcast(this, RecipeTag);
}

void USimEco_FacilityComponent::AdvanceEconomyStep(double StepSeconds, int64 StepIndex, int32 DayNumber)
{
	// Called only on the server by the driver, but guard defensively.
	AActor* Owner = GetOwner();
	if (!Owner || !Owner->HasAuthority())
	{
		return;
	}

	// Start the head job if idle (StartStepIndex is the step at which it begins).
	TryStartHeadJob(StepIndex);

	// Complete the running job once enough steps have elapsed.
	const FSimEco_JobEntry* Head = Jobs.Entries.FindByPredicate(
		[](const FSimEco_JobEntry& E) { return E.State == ESimEco_JobState::Running; });
	if (Head && Head->StartStepIndex >= 0 && Head->StepsRequired > 0)
	{
		const int64 Elapsed = StepIndex - Head->StartStepIndex;
		if (Elapsed >= Head->StepsRequired)
		{
			CompleteHeadJob();
		}
	}
}
