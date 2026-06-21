// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Building/UBuild_FacilityProducerComponent.h"
#include "Resource/USurv_ResourceStoreComponent.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"

UBuild_FacilityProducerComponent::UBuild_FacilityProducerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBuild_FacilityProducerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthorityToMutate() && bAutoStartFirstProcess && Processes.Num() > 0)
	{
		SetActiveProcess_Implementation(Processes[0].ProcessTag);
	}
}

void UBuild_FacilityProducerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CycleTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}

void UBuild_FacilityProducerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBuild_FacilityProducerComponent, ActiveProcessTag);
	DOREPLIFETIME(UBuild_FacilityProducerComponent, CycleStartServerTime);
}

bool UBuild_FacilityProducerComponent::HasAuthorityToMutate() const
{
	return GetOwner() && GetOwner()->HasAuthority();
}

double UBuild_FacilityProducerComponent::GetWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

const FBuild_ProductionProcess* UBuild_FacilityProducerComponent::FindProcess(const FGameplayTag& ProcessTag) const
{
	for (const FBuild_ProductionProcess& Process : Processes)
	{
		if (Process.ProcessTag == ProcessTag)
		{
			return &Process;
		}
	}
	return nullptr;
}

// ---- ISeam_ResourceProducer ----

float UBuild_FacilityProducerComponent::GetProductionProgress_Implementation() const
{
	if (!ActiveProcessTag.IsValid())
	{
		return 0.f;
	}
	const FBuild_ProductionProcess* Process = FindProcess(ActiveProcessTag);
	const float Cycle = Process ? FMath::Max(0.1f, Process->CycleSeconds) : FMath::Max(0.1f, ActiveCycleSeconds);
	const double Elapsed = GetWorldTimeSeconds() - CycleStartServerTime;
	return FMath::Clamp(static_cast<float>(Elapsed) / Cycle, 0.f, 1.f);
}

void UBuild_FacilityProducerComponent::GetExpectedOutputs_Implementation(TArray<FGameplayTag>& OutCommodities, TArray<float>& OutQuantities) const
{
	OutCommodities.Reset();
	OutQuantities.Reset();
	if (const FBuild_ProductionProcess* Process = FindProcess(ActiveProcessTag))
	{
		for (const FSurv_ResourceStack& Out : Process->Outputs)
		{
			OutCommodities.Add(Out.ItemTag);
			OutQuantities.Add(static_cast<float>(Out.Count));
		}
	}
}

bool UBuild_FacilityProducerComponent::SetActiveProcess_Implementation(FGameplayTag ProcessTag)
{
	if (!HasAuthorityToMutate())
	{
		return false;
	}
	const FBuild_ProductionProcess* Process = FindProcess(ProcessTag);
	if (!Process)
	{
		UE_LOG(LogDP, Warning, TEXT("[Survival] Facility: unknown process %s"), *ProcessTag.ToString());
		return false;
	}
	ActiveProcessTag = ProcessTag;
	ActiveCycleSeconds = FMath::Max(0.1f, Process->CycleSeconds);
	BeginCycle();
	return true;
}

void UBuild_FacilityProducerComponent::CancelProduction_Implementation()
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CycleTimerHandle);
	}
	ActiveProcessTag = FGameplayTag();
	CycleStartServerTime = 0.0;
	ActiveCycleSeconds = 0.f;
}

void UBuild_FacilityProducerComponent::BeginCycle()
{
	if (!HasAuthorityToMutate() || !ActiveProcessTag.IsValid())
	{
		return;
	}
	CycleStartServerTime = GetWorldTimeSeconds();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			CycleTimerHandle, this, &UBuild_FacilityProducerComponent::HandleCycleComplete,
			ActiveCycleSeconds, /*bLoop*/ false);
	}
}

void UBuild_FacilityProducerComponent::HandleCycleComplete()
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	if (const FBuild_ProductionProcess* Process = FindProcess(ActiveProcessTag))
	{
		if (OutputStore)
		{
			for (const FSurv_ResourceStack& Out : Process->Outputs)
			{
				if (Out.ItemTag.IsValid() && Out.Count > 0)
				{
					OutputStore->AddResource(Out.ItemTag, Out.Count);
				}
			}
		}
		// Start the next cycle of the same process (continuous production).
		BeginCycle();
	}
}

void UBuild_FacilityProducerComponent::OnRep_Cycle()
{
	// Clients have nothing authoritative to do; progress is derived in GetProductionProgress. This
	// hook exists so UI can refresh when the active process / cycle start changes.
}
