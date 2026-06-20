// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Behavior/AI_BehaviorComponent.h"

#include "DesignPatternsAINativeTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "FSM/DPStateMachineComponent.h"
#include "FSM/DPBlackboard.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Identity/Seam_EntityIdentity.h"

#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

// Engine AIModule types — PRIVATE dependency, included only here in the .cpp.
#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UAI_BehaviorComponent::UAI_BehaviorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Carries a replicated authoritative decision tag.
	SetIsReplicatedByDefault(true);
}

void UAI_BehaviorComponent::BeginPlay()
{
	Super::BeginPlay();

	// Resolve the FSM up front for the StateMachine backend so designers see a clear log if missing.
	if (Backend == EAI_BrainBackend::StateMachine)
	{
		if (UDP_StateMachineComponent* SM = ResolveStateMachine())
		{
			// Keep the decision tag in sync with autonomous FSM transitions (authority only).
			if (HasAuthoritySafe())
			{
				SM->OnStateChanged.AddDynamic(this, &UAI_BehaviorComponent::HandleFsmStateChanged);
			}
		}
		else
		{
			UE_LOG(LogDP, Warning, TEXT("[AI.Behavior] %s uses StateMachine backend but no UDP_StateMachineComponent was found."),
				*GetNameSafe(GetOwner()));
		}
	}
	else if (Backend == EAI_BrainBackend::BehaviorTree && !BehaviorTreeAsset)
	{
		UE_LOG(LogDP, Warning, TEXT("[AI.Behavior] %s uses BehaviorTree backend but BehaviorTreeAsset is unset."),
			*GetNameSafe(GetOwner()));
	}

	// On authority, kick the first decision so CurrentDecision is populated and replicated.
	if (HasAuthoritySafe() && bDecisionEnabled)
	{
		RequestDecision();
	}
}

void UAI_BehaviorComponent::HandleFsmStateChanged(FGameplayTag From, FGameplayTag To)
{
	// Only the authority owns the decision; the resulting tag replicates to clients.
	if (!HasAuthoritySafe() || !bDecisionEnabled)
	{
		return;
	}
	SetDecisionAuthoritative(To);
}

void UAI_BehaviorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Backend == EAI_BrainBackend::StateMachine)
	{
		if (UDP_StateMachineComponent* SM = ResolveStateMachine())
		{
			SM->OnStateChanged.RemoveDynamic(this, &UAI_BehaviorComponent::HandleFsmStateChanged);
		}
	}

	if (bBehaviorTreeRunning)
	{
		if (AAIController* Controller = ResolveAIController())
		{
			if (UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(Controller->GetBrainComponent()))
			{
				BTComp->StopLogic(TEXT("AI_BehaviorComponent EndPlay"));
			}
		}
		bBehaviorTreeRunning = false;
	}

	Super::EndPlay(EndPlayReason);
}

void UAI_BehaviorComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAI_BehaviorComponent, CurrentDecision);
}

void UAI_BehaviorComponent::RequestDecision()
{
	// AUTHORITY ONLY — decisions are authoritative gameplay state.
	if (!HasAuthoritySafe())
	{
		return;
	}
	if (!bDecisionEnabled)
	{
		return;
	}

	switch (Backend)
	{
	case EAI_BrainBackend::StateMachine:
	{
		if (UDP_StateMachineComponent* SM = ResolveStateMachine())
		{
			// Ensure a valid active state on first decision without wiping in-progress FSM state.
			if (!SM->GetActiveStateTag().IsValid())
			{
				SM->RestartToInitialState();
			}
			// The FSM evaluates its own transitions on tick; here we sample the current state tag,
			// which IS this brain's current decision.
			SetDecisionAuthoritative(SM->GetActiveStateTag());
		}
		break;
	}
	case EAI_BrainBackend::BehaviorTree:
	{
		AAIController* Controller = ResolveAIController();
		if (Controller && BehaviorTreeAsset)
		{
			if (!bBehaviorTreeRunning)
			{
				Controller->RunBehaviorTree(BehaviorTreeAsset);
				bBehaviorTreeRunning = true;
			}
			// Mirror the decision key from the BT blackboard if configured.
			if (!BlackboardKey_Decision.IsNone())
			{
				if (UBlackboardComponent* BB = Controller->GetBlackboardComponent())
				{
					const FName DecisionName = BB->GetValueAsName(BlackboardKey_Decision);
					FGameplayTag Decision;
					if (!DecisionName.IsNone())
					{
						Decision = FGameplayTag::RequestGameplayTag(DecisionName, /*ErrorIfNotFound*/ false);
					}
					SetDecisionAuthoritative(Decision);
				}
			}
		}
		break;
	}
	case EAI_BrainBackend::None:
	default:
		SetDecisionAuthoritative(FGameplayTag());
		break;
	}
}

void UAI_BehaviorComponent::SetDecisionEnabled(bool bEnabled)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	if (bDecisionEnabled == bEnabled)
	{
		return;
	}
	bDecisionEnabled = bEnabled;

	if (!bEnabled)
	{
		// Pause the backend and clear the decision.
		if (Backend == EAI_BrainBackend::BehaviorTree && bBehaviorTreeRunning)
		{
			if (AAIController* Controller = ResolveAIController())
			{
				if (UBehaviorTreeComponent* BTComp = Cast<UBehaviorTreeComponent>(Controller->GetBrainComponent()))
				{
					BTComp->StopLogic(TEXT("AI_BehaviorComponent disabled"));
				}
			}
			bBehaviorTreeRunning = false;
		}
		SetDecisionAuthoritative(FGameplayTag());
	}
	else
	{
		RequestDecision();
	}
}

void UAI_BehaviorComponent::SetTargetEntity(FSeam_EntityId Target)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	TargetEntity = Target;
	PublishTargetToBlackboard();
}

void UAI_BehaviorComponent::PublishTargetToBlackboard()
{
	if (BlackboardKey_TargetEntity.IsNone())
	{
		return;
	}

	const FString TargetString = TargetEntity.IsValid() ? TargetEntity.ToString() : FString();

	if (Backend == EAI_BrainBackend::StateMachine)
	{
		if (UDP_StateMachineComponent* SM = ResolveStateMachine())
		{
			if (UDP_Blackboard* BB = SM->GetBlackboard())
			{
				// The core blackboard is typed (no string slot); publish whether a valid target exists
				// so FSM guards can branch. The concrete id travels over the bus / threat seam instead.
				BB->SetBool(BlackboardKey_TargetEntity, TargetEntity.IsValid());
			}
		}
	}
	else if (Backend == EAI_BrainBackend::BehaviorTree)
	{
		if (AAIController* Controller = ResolveAIController())
		{
			if (UBlackboardComponent* BB = Controller->GetBlackboardComponent())
			{
				BB->SetValueAsName(BlackboardKey_TargetEntity, FName(*TargetString));
			}
		}
	}
}

void UAI_BehaviorComponent::SetDecisionAuthoritative(FGameplayTag NewDecision)
{
	if (CurrentDecision == NewDecision)
	{
		return;
	}
	const FGameplayTag Previous = CurrentDecision;
	CurrentDecision = NewDecision;
	HandleDecisionChanged(Previous, NewDecision);
}

void UAI_BehaviorComponent::OnRep_CurrentDecision(FGameplayTag PreviousDecision)
{
	HandleDecisionChanged(PreviousDecision, CurrentDecision);
}

void UAI_BehaviorComponent::HandleDecisionChanged(FGameplayTag PreviousDecision, FGameplayTag NewDecision)
{
	UE_LOG(LogDP, Verbose, TEXT("[AI.Behavior] %s decision %s -> %s"),
		*GetNameSafe(GetOwner()), *PreviousDecision.ToString(), *NewDecision.ToString());

	OnDecisionChanged.Broadcast(PreviousDecision, NewDecision);

	if (bBroadcastOnBus)
	{
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FAI_DecisionChangedPayload Payload;
			Payload.AgentId = GetOwnerEntityId();
			Payload.Decision = NewDecision;
			Payload.Backend = Backend;

			const FInstancedStruct PayloadStruct = FInstancedStruct::Make(Payload);
			Bus->BroadcastPayload(AINativeTags::Bus_AI_DecisionChanged, PayloadStruct, this);
		}
	}
}

AAIController* UAI_BehaviorComponent::ResolveAIController() const
{
	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		return Cast<AAIController>(Pawn->GetController());
	}
	return nullptr;
}

UDP_StateMachineComponent* UAI_BehaviorComponent::ResolveStateMachine() const
{
	if (StateMachine)
	{
		return StateMachine;
	}
	if (const AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<UDP_StateMachineComponent>();
	}
	return nullptr;
}

FSeam_EntityId UAI_BehaviorComponent::GetOwnerEntityId() const
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

bool UAI_BehaviorComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
