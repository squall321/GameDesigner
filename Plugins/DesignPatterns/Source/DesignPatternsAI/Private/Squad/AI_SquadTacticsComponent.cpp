// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Squad/AI_SquadTacticsComponent.h"
#include "Seams/AI_Squad.h"
#include "Tactical/AI_TacticalBusPayloads.h"
#include "DesignPatternsAINativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "FSM/DPBlackboard.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Identity/Seam_EntityIdentity.h"

// World module is a PRIVATE dependency — resolved here in the .cpp only (per-squad hub blackboard).
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_Scope.h"
#include "Blackboard/WorldHub_ScopedBlackboard.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject on 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UAI_SquadTacticsComponent::UAI_SquadTacticsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	// No replicated state: slots replicate on the carrier; tactics derive locally on the authority.
}

void UAI_SquadTacticsComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UAI_SquadTacticsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!HasAuthoritySafe() || !ActiveTactic.IsValid())
	{
		return;
	}

	// Re-run the active tactic so the destination tracks the moving squad/anchor.
	ExecuteTactic(ActiveTactic);

	// Bounding-overwatch turn advance on the per-squad hub blackboard.
	BoundingAccumulator += DeltaTime;
	if (BoundingAccumulator >= BoundingOverwatchInterval)
	{
		BoundingAccumulator = 0.f;
		AdvanceBoundingTurn();
	}
}

void UAI_SquadTacticsComponent::SetSquad(FGuid InSquadId)
{
	SquadId = InSquadId;
}

//~ Tactic execution ----------------------------------------------------------------------------

void UAI_SquadTacticsComponent::ExecuteTactic(FGameplayTag TacticTag)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const bool bNewTactic = (TacticTag != ActiveTactic);
	ActiveTactic = TacticTag;

	const FTransform Slot = ResolveAssignedSlot();
	FVector Destination = Slot.GetLocation();

	// Generic tactic behaviours, recognised by tag NAME match against the AI.Tactic vocabulary so designers
	// can author leaves. Unknown tactics hold the assigned slot.
	const FString TacticName = TacticTag.IsValid() ? TacticTag.ToString() : FString();
	if (TacticName.Contains(TEXT("Advance")))
	{
		// Advance: push the slot forward (along the slot's facing) by one step — but only when it is our
		// turn in the bounding rotation, so the squad bounds rather than rushing as one.
		const int32 Turn = ReadBoundingTurn();
		const bool bMyTurn = (FMath::Abs(GetOwnerEntityId().Value.A) % 2) == (Turn % 2);
		if (bMyTurn)
		{
			Destination += Slot.GetRotation().GetForwardVector() * FMath::Max(1.f, AdvanceStepDistance);
		}
	}
	else if (TacticName.Contains(TEXT("Suppress")))
	{
		// Suppress: hold the slot exactly (the FSM's suppress state fires from here). Destination == slot.
	}

	if (IDP_BlackboardProvider* Board = ResolveBlackboardProvider())
	{
		Board->SetVector(BlackboardKey_SlotLocation, Destination);
	}

	if (bNewTactic && bBroadcastOnBus)
	{
		BroadcastTacticOnBus(TacticTag);
	}
}

FTransform UAI_SquadTacticsComponent::ResolveAssignedSlot() const
{
	if (TScriptInterface<IAI_Squad> Squad = ResolveSquadSeam())
	{
		return Squad->GetFormationSlot(GetOwnerEntityId());
	}
	return FTransform::Identity;
}

//~ Bounding-overwatch hub coordination ---------------------------------------------------------

int32 UAI_SquadTacticsComponent::ReadBoundingTurn() const
{
	if (!SquadId.IsValid())
	{
		return 0;
	}
	if (UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this))
	{
		const FWorldHub_Scope Scope = FWorldHub_Scope::Entity(FSeam_EntityId(SquadId));
		if (UWorldHub_ScopedBlackboard* Board = Hub->GetBlackboard(Scope, /*bCreate=*/false))
		{
			return Board->GetInt(HubKey_BoundingTurn, 0);
		}
	}
	return 0;
}

void UAI_SquadTacticsComponent::AdvanceBoundingTurn()
{
	if (!HasAuthoritySafe() || !SquadId.IsValid())
	{
		return;
	}
	if (UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this))
	{
		const FWorldHub_Scope Scope = FWorldHub_Scope::Entity(FSeam_EntityId(SquadId));
		if (UWorldHub_ScopedBlackboard* Board = Hub->GetBlackboard(Scope, /*bCreate=*/true))
		{
			Board->SetInt(HubKey_BoundingTurn, Board->GetInt(HubKey_BoundingTurn, 0) + 1);
		}
	}
}

//~ Resolution helpers --------------------------------------------------------------------------

TScriptInterface<IAI_Squad> UAI_SquadTacticsComponent::ResolveSquadSeam() const
{
	TScriptInterface<IAI_Squad> Result;
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return Result;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(Owner))
	{
		if (UObject* SquadObj = Locator->ResolveService(AINativeTags::Service_AI_Squad))
		{
			if (SquadObj->GetClass()->ImplementsInterface(UAI_Squad::StaticClass()))
			{
				Result.SetObject(SquadObj);
				Result.SetInterface(Cast<IAI_Squad>(SquadObj));
			}
		}
	}
	return Result;
}

IDP_BlackboardProvider* UAI_SquadTacticsComponent::ResolveBlackboardProvider() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (Owner->GetClass()->ImplementsInterface(UDP_BlackboardProvider::StaticClass()))
	{
		return Cast<IDP_BlackboardProvider>(Owner);
	}
	if (UActorComponent* Comp = Owner->FindComponentByInterface(UDP_BlackboardProvider::StaticClass()))
	{
		return Cast<IDP_BlackboardProvider>(Comp);
	}
	return nullptr;
}

FSeam_EntityId UAI_SquadTacticsComponent::GetOwnerEntityId() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FSeam_EntityId::Invalid();
	}
	if (Owner->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(Owner);
	}
	if (UActorComponent* Comp = Owner->FindComponentByInterface(USeam_EntityIdentity::StaticClass()))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(Comp);
	}
	return FSeam_EntityId::Invalid();
}

void UAI_SquadTacticsComponent::BroadcastTacticOnBus(FGameplayTag TacticTag) const
{
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FAI_TacticPayload Payload;
		Payload.AgentId = GetOwnerEntityId();
		Payload.SquadId = SquadId;
		Payload.TacticTag = TacticTag;
		Bus->BroadcastPayload(AINativeTags::Bus_AI_Tactic, FInstancedStruct::Make(Payload), this);
	}
}

bool UAI_SquadTacticsComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
