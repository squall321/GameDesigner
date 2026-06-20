// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Perception/AI_PerceptionComponent.h"
#include "Perception/AI_PerceptionListenerProxy.h"

#include "DesignPatternsAINativeTags.h"
#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "FSM/DPBlackboard.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Identity/Seam_EntityIdentity.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"

// Engine AIModule types — PRIVATE dependency, included only here in the .cpp.
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISense.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Damage.h"

// World hub concrete type — PRIVATE dependency, resolved via the locator and used only here.
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_Scope.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

void UAI_PerceptionListenerProxy::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
	UAI_PerceptionComponent* Adapter = Owner.Get();
	if (!Adapter)
	{
		return;
	}

	// Map the engine sense class to an AI.Percept.* tag.
	FGameplayTag SenseTag;
	const TSubclassOf<UAISense> SenseClass = UAIPerceptionSystem::GetSenseClassForStimulus(Adapter, Stimulus);
	if (SenseClass == UAISense_Sight::StaticClass())
	{
		SenseTag = AINativeTags::AI_Percept_Sight;
	}
	else if (SenseClass == UAISense_Hearing::StaticClass())
	{
		SenseTag = AINativeTags::AI_Percept_Hearing;
	}
	else if (SenseClass == UAISense_Damage::StaticClass())
	{
		SenseTag = AINativeTags::AI_Percept_Damage;
	}
	else
	{
		// Unknown/extension sense: classify under the percept root so listeners still see it.
		SenseTag = AINativeTags::AI_Percept;
	}

	const float Normalized = FMath::Clamp(Stimulus.Strength, 0.f, 1.f);
	Adapter->IngestStimulus(Actor, SenseTag, Stimulus.WasSuccessfullySensed(),
		Stimulus.StimulusLocation, Normalized);
}

UAI_PerceptionComponent::UAI_PerceptionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Cosmetic/local adapter over authoritative sensing: nothing here replicates directly.
	SetIsReplicatedByDefault(false);
}

void UAI_PerceptionComponent::OnRegister()
{
	Super::OnRegister();

	// Create the engine sensing component as an instanced subobject so GC owns it.
	if (!InnerPerception)
	{
		InnerPerception = NewObject<UAIPerceptionComponent>(this, TEXT("InnerAIPerception"));
		if (InnerPerception)
		{
			InnerPerception->RegisterComponent();
		}
	}

	// Create the listener proxy and wire the engine delegate to it.
	if (InnerPerception && !PerceptionListener)
	{
		UAI_PerceptionListenerProxy* Proxy = NewObject<UAI_PerceptionListenerProxy>(this, TEXT("AIPerceptionListener"));
		Proxy->Owner = this;
		PerceptionListener = Proxy;
		InnerPerception->OnTargetPerceptionUpdated.AddDynamic(Proxy, &UAI_PerceptionListenerProxy::OnTargetPerceptionUpdated);
	}
}

void UAI_PerceptionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!HasAuthoritySafe())
	{
		// Sensing is authority-driven; clients do no work and react to replicated gameplay instead.
		UE_LOG(LogDP, Verbose, TEXT("[AI.Perception] %s skipping sensing on non-authority."),
			*GetNameSafe(GetOwner()));
	}
}

void UAI_PerceptionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (InnerPerception && PerceptionListener)
	{
		if (UAI_PerceptionListenerProxy* Proxy = Cast<UAI_PerceptionListenerProxy>(PerceptionListener))
		{
			InnerPerception->OnTargetPerceptionUpdated.RemoveDynamic(Proxy, &UAI_PerceptionListenerProxy::OnTargetPerceptionUpdated);
		}
	}

	// Stop listening on the bus / drop our blackboard summary references.
	Percepts.Reset();
	PerceptionListener = nullptr;

	Super::EndPlay(EndPlayReason);
}

void UAI_PerceptionComponent::IngestStimulus(AActor* Actor, FGameplayTag SenseTag, bool bSensed,
	const FVector& StimulusLocation, float NormalizedStrength)
{
	if (!HasAuthoritySafe())
	{
		return;
	}

	FAI_Percept Percept;
	Percept.SenseTag = SenseTag;
	Percept.SourceActor = Actor;
	Percept.SourceId = GetEntityIdFor(Actor);
	Percept.bSensed = bSensed;
	Percept.LastKnownLocation = StimulusLocation;
	Percept.Strength = NormalizedStrength;
	Percept.UpdatedAtTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	ApplyPercept(Percept);
}

void UAI_PerceptionComponent::ApplyPercept(const FAI_Percept& Percept)
{
	// Find an existing entry for this (actor, sense) pair.
	const int32 Index = Percepts.IndexOfByPredicate([&Percept](const FAI_Percept& Existing)
	{
		return Existing.SourceActor == Percept.SourceActor && Existing.SenseTag == Percept.SenseTag;
	});

	if (Percept.bSensed)
	{
		if (Index != INDEX_NONE)
		{
			Percepts[Index] = Percept;
		}
		else
		{
			Percepts.Add(Percept);
		}
	}
	else if (Index != INDEX_NONE)
	{
		// Lost: remove from the live cache (we still fire the delegate/bus with bSensed=false).
		Percepts.RemoveAtSwap(Index);
	}

	OnPerceptUpdated.Broadcast(Percept);

	if (bBroadcastOnBus)
	{
		BroadcastPerceptOnBus(Percept);
	}

	PushSummary();
}

void UAI_PerceptionComponent::PushSummary()
{
	// Compute the strongest currently-sensed percept (any sense) for the summary.
	const FAI_Percept* Best = nullptr;
	for (const FAI_Percept& P : Percepts)
	{
		if (P.bSensed && (!Best || P.Strength > Best->Strength))
		{
			Best = &P;
		}
	}
	const bool bHasTarget = (Best != nullptr);

	if (bPushToBlackboard)
	{
		if (IDP_BlackboardProvider* BB = ResolveBlackboardProvider())
		{
			BB->SetBool(BlackboardKey_HasTarget, bHasTarget);
			if (bHasTarget)
			{
				BB->SetVector(BlackboardKey_TargetLocation, Best->LastKnownLocation);
				BB->SetObject(BlackboardKey_TargetActor, Best->SourceActor.Get());
			}
			else
			{
				BB->ClearKey(BlackboardKey_TargetActor);
			}
		}
	}

	if (bPushToWorldHub && WorldHubKey_HasTarget.IsValid())
	{
		if (UWorldHub_StateHubSubsystem* Hub = FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(this))
		{
			const FSeam_EntityId OwnerId = GetOwnerEntityId();
			if (OwnerId.IsValid())
			{
				// SetFlag is authority-guarded inside the hub; we already gated on authority.
				Hub->SetFlag(WorldHubKey_HasTarget, bHasTarget, FWorldHub_Scope::Entity(OwnerId));
			}
		}
	}
}

void UAI_PerceptionComponent::BroadcastPerceptOnBus(const FAI_Percept& Percept) const
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FAI_PerceptUpdatedPayload Payload;
	Payload.SensingAgentId = GetOwnerEntityId();
	Payload.SourceId = Percept.SourceId;
	Payload.SenseTag = Percept.SenseTag;
	Payload.bSensed = Percept.bSensed;
	Payload.LastKnownLocation = Percept.LastKnownLocation;

	const FInstancedStruct PayloadStruct = FInstancedStruct::Make(Payload);
	Bus->BroadcastPayload(AINativeTags::Bus_AI_PerceptUpdated, PayloadStruct, const_cast<UAI_PerceptionComponent*>(this));
}

TArray<FAI_Percept> UAI_PerceptionComponent::GetActivePercepts() const
{
	TArray<FAI_Percept> Out;
	Out.Reserve(Percepts.Num());
	for (const FAI_Percept& P : Percepts)
	{
		if (P.bSensed)
		{
			Out.Add(P);
		}
	}
	return Out;
}

bool UAI_PerceptionComponent::GetStrongestPercept(FGameplayTag SenseTag, FAI_Percept& OutPercept) const
{
	const FAI_Percept* Best = nullptr;
	for (const FAI_Percept& P : Percepts)
	{
		if (!P.bSensed)
		{
			continue;
		}
		if (SenseTag.IsValid() && !P.SenseTag.MatchesTag(SenseTag))
		{
			continue;
		}
		if (!Best || P.Strength > Best->Strength)
		{
			Best = &P;
		}
	}
	if (Best)
	{
		OutPercept = *Best;
		return true;
	}
	return false;
}

bool UAI_PerceptionComponent::HasPerceptFor(FGameplayTag SenseTag) const
{
	for (const FAI_Percept& P : Percepts)
	{
		if (P.bSensed && (!SenseTag.IsValid() || P.SenseTag.MatchesTag(SenseTag)))
		{
			return true;
		}
	}
	return false;
}

FSeam_EntityId UAI_PerceptionComponent::GetOwnerEntityId() const
{
	return GetEntityIdFor(GetOwner());
}

FSeam_EntityId UAI_PerceptionComponent::GetEntityIdFor(const AActor* Actor)
{
	if (!Actor)
	{
		return FSeam_EntityId::Invalid();
	}

	// The identity seam may be on the actor or one of its components.
	if (Actor->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(const_cast<AActor*>(Actor));
	}
	if (UActorComponent* Comp = const_cast<AActor*>(Actor)->FindComponentByInterface(USeam_EntityIdentity::StaticClass()))
	{
		return ISeam_EntityIdentity::Execute_GetEntityId(Comp);
	}
	return FSeam_EntityId::Invalid();
}

IDP_BlackboardProvider* UAI_PerceptionComponent::ResolveBlackboardProvider() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (IDP_BlackboardProvider* OnActor = Cast<IDP_BlackboardProvider>(Owner))
	{
		return OnActor;
	}
	if (UActorComponent* Comp = Owner->FindComponentByInterface(UDP_BlackboardProvider::StaticClass()))
	{
		return Cast<IDP_BlackboardProvider>(Comp);
	}
	return nullptr;
}

bool UAI_PerceptionComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
