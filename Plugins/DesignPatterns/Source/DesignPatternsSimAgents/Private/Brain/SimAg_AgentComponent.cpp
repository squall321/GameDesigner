// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Brain/SimAg_AgentComponent.h"
#include "Save/SimAg_SaveGame.h"
#include "DesignPatternsSimAgentsTags.h"
#include "Settings/SimAg_DeveloperSettings.h"
#include "Strategy/DPStrategySelector.h"
#include "Brain/SimAg_BrainTypes.h"
#include "FSM/DPBlackboard.h"
#include "Identity/Seam_EntityIdentity.h"
#include "Core/DPLog.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

USimAg_AgentComponent::USimAg_AgentComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);

	// Construct the owned blackboard subobject so the strategy context is always valid.
	Blackboard = CreateDefaultSubobject<UDP_Blackboard>(TEXT("AgentBlackboard"));
}

void USimAg_AgentComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(USimAg_AgentComponent, CurrentActivity);
}

void USimAg_AgentComponent::BeginPlay()
{
	Super::BeginPlay();

	if (const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get())
	{
		DecisionPeriod = 1.f / FMath::Max(0.1f, Settings->DecisionTickHz);
	}

	EnsureBrain();

	// Assign / adopt a stable id on authority. Prefer the entity-identity seam off the owner so the agent
	// shares one id with the rest of its systems; otherwise mint a fresh one.
	if (HasOwnerAuthority())
	{
		if (AActor* Owner = GetOwner())
		{
			if (Owner->Implements<USeam_EntityIdentity>())
			{
				AgentId = ISeam_EntityIdentity::Execute_GetEntityId(Owner);
			}
		}
		if (!AgentId.IsValid())
		{
			AgentId = FSeam_EntityId::NewId();
		}
	}

	// Stagger decision evaluations across agents so a crowd does not all think on the same frame.
	DecisionAccumulator = FMath::FRand() * DecisionPeriod;
}

void USimAg_AgentComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

void USimAg_AgentComponent::EnsureBrain()
{
	if (Brain)
	{
		return;
	}
	const USimAg_DeveloperSettings* Settings = USimAg_DeveloperSettings::Get();
	if (!Settings)
	{
		return;
	}
	// Soft default brain: only loads when an agent without its own brain is spawned.
	if (UClass* BrainClass = Settings->DefaultBrainClass.LoadSynchronous())
	{
		Brain = NewObject<UDP_StrategySelector>(this, BrainClass, TEXT("DefaultBrain"));
	}
	if (!Brain)
	{
		UE_LOG(LogDP, Verbose, TEXT("SimAg agent '%s' has no brain (no inline selector and no DefaultBrainClass)."),
			*GetNameSafe(GetOwner()));
	}
}

void USimAg_AgentComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// The brain runs ONLY on authority; clients merely observe the replicated activity.
	if (!HasOwnerAuthority())
	{
		return;
	}

	DecisionAccumulator += DeltaTime;
	if (DecisionAccumulator >= DecisionPeriod)
	{
		DecisionAccumulator = 0.f;
		RunDecision();
	}
}

void USimAg_AgentComponent::RunDecision()
{
	if (!Brain || !Blackboard)
	{
		return;
	}
	// Build a fresh context each pass: weak owner + the blackboard provider seam.
	const FDP_StrategyContext Context(GetOwner(), GetBlackboardProvider());
	// SelectAndExecute polls each strategy's side-effect-free ScoreFor, then runs the winner's Execute,
	// which writes the chosen MoveTarget into the blackboard and the activity onto this component.
	Brain->SelectAndExecute(Context);
}

bool USimAg_AgentComponent::HasOwnerAuthority() const
{
	const AActor* Owner = GetOwner();
	return Owner && (Owner->GetLocalRole() == ROLE_Authority);
}

//~ ISimAg_Agent --------------------------------------------------------------------------------

FGameplayTag USimAg_AgentComponent::GetAgentTag_Implementation() const
{
	return AgentTag;
}

FGameplayTag USimAg_AgentComponent::GetCurrentActivity_Implementation() const
{
	return CurrentActivity;
}

FVector USimAg_AgentComponent::GetHomeLocation_Implementation() const
{
	return HomeLocation;
}

FVector USimAg_AgentComponent::GetWorkLocation_Implementation() const
{
	return WorkLocation;
}

//~ State mutators ------------------------------------------------------------------------------

void USimAg_AgentComponent::SetCurrentActivity(FGameplayTag NewActivity)
{
	// AUTHORITY GUARD at top.
	if (!HasOwnerAuthority())
	{
		return;
	}
	if (CurrentActivity == NewActivity)
	{
		return;
	}
	CurrentActivity = NewActivity;
	// Fire locally on the server immediately; OnRep fires the same delegate on clients.
	OnActivityChanged.Broadcast(this, CurrentActivity);
}

void USimAg_AgentComponent::SetClaimedJob(const FSimAg_JobHandle& Handle)
{
	if (!HasOwnerAuthority())
	{
		return;
	}
	ClaimedJobId = Handle.JobId;
	if (Blackboard)
	{
		Blackboard->SetBool(SimAg_BrainKeys::ClaimedJobActive, Handle.IsValid());
	}
}

void USimAg_AgentComponent::OnRep_CurrentActivity()
{
	OnActivityChanged.Broadcast(this, CurrentActivity);
}

TScriptInterface<IDP_BlackboardProvider> USimAg_AgentComponent::GetBlackboardProvider() const
{
	TScriptInterface<IDP_BlackboardProvider> Provider;
	if (Blackboard)
	{
		Provider.SetObject(Blackboard);
		Provider.SetInterface(Cast<IDP_BlackboardProvider>(Blackboard));
	}
	return Provider;
}

//~ ISeam_Persistable ---------------------------------------------------------------------------

void USimAg_AgentComponent::CaptureState_Implementation(FInstancedStruct& Out) const
{
	FSimAg_AgentRecord Record;
	Record.AgentId = AgentId;
	Record.AgentTag = AgentTag;
	Record.CurrentActivity = CurrentActivity;
	if (Blackboard && Blackboard->HasKey(SimAg_BrainKeys::MoveTarget))
	{
		Record.MoveTarget = Blackboard->GetVector(SimAg_BrainKeys::MoveTarget);
		Record.bHasMoveTarget = true;
	}
	Out = FInstancedStruct::Make(Record);
}

void USimAg_AgentComponent::RestoreState_Implementation(const FInstancedStruct& In)
{
	// AUTHORITY GUARD: a client load is a no-op (state arrives via replication).
	if (!HasOwnerAuthority())
	{
		return;
	}
	const FSimAg_AgentRecord* Record = In.GetPtr<FSimAg_AgentRecord>();
	if (!Record)
	{
		return;
	}
	AgentId = Record->AgentId;
	AgentTag = Record->AgentTag;
	SetCurrentActivity(Record->CurrentActivity);
	if (Record->bHasMoveTarget && Blackboard)
	{
		Blackboard->SetVector(SimAg_BrainKeys::MoveTarget, Record->MoveTarget);
		Blackboard->SetBool(SimAg_BrainKeys::IsMoving, true);
	}
}

FGameplayTag USimAg_AgentComponent::GetPersistenceKind_Implementation() const
{
	return SimAgNativeTags::Persist_Agent;
}
