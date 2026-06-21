// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Threat/AI_ThreatShareComponent.h"
#include "Threat/AI_ThreatComponent.h"
#include "Seams/AI_Squad.h"
#include "Seams/AI_Threatened.h"
#include "Seams/AI_Brain.h"
#include "DesignPatternsAINativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#include "Identity/Seam_EntityIdentity.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

UAI_ThreatShareComponent::UAI_ThreatShareComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	SetIsReplicatedByDefault(true);
}

void UAI_ThreatShareComponent::BeginPlay()
{
	Super::BeginPlay();

	// Listen for our own threat-table changes so we can re-drive the brain target (authority only acts).
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->ListenNative(AINativeTags::Bus_AI_ThreatChanged,
			[this](const FDP_Message& Message) { HandleThreatChanged(Message); }, this);
	}
}

void UAI_ThreatShareComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListeningForOwner(this);
	}
	Super::EndPlay(EndPlayReason);
}

void UAI_ThreatShareComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!HasAuthoritySafe())
	{
		return;
	}

	EvaluateAccumulator += DeltaTime;
	if (EvaluateAccumulator < EvaluateInterval)
	{
		return;
	}
	EvaluateAccumulator = 0.f;

	// Expire an active taunt.
	if (TauntForcedTarget.IsValid())
	{
		const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		if (Now >= TauntExpiryTime)
		{
			TauntForcedTarget = FSeam_EntityId::Invalid();
		}
	}

	if (bDriveBrainTarget)
	{
		DriveBrainTarget();
	}
}

void UAI_ThreatShareComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAI_ThreatShareComponent, TauntForcedTarget);
}

//~ ISeam_TauntSink -----------------------------------------------------------------------------

void UAI_ThreatShareComponent::ForceTaunt(FSeam_EntityId Source, float Duration)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	if (Duration <= 0.f || !Source.IsValid())
	{
		TauntForcedTarget = FSeam_EntityId::Invalid();
		TauntExpiryTime = 0.0;
	}
	else
	{
		TauntForcedTarget = Source;
		TauntExpiryTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + static_cast<double>(Duration);
	}

	if (bDriveBrainTarget)
	{
		DriveBrainTarget();
	}
}

//~ Public actions ------------------------------------------------------------------------------

void UAI_ThreatShareComponent::ApplyTaunt(FSeam_EntityId Tauntee, FSeam_EntityId ForcedTarget, float Duration)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	// Self-taunt is just ForceTaunt; otherwise forward to the tauntee's own sink.
	if (Tauntee == GetOwnerEntityId() || !Tauntee.IsValid())
	{
		ForceTaunt(ForcedTarget, Duration);
		return;
	}
	if (TScriptInterface<ISeam_TauntSink> Sink = ResolveTauntSinkById(Tauntee))
	{
		Sink->ForceTaunt(ForcedTarget, Duration);
	}
}

void UAI_ThreatShareComponent::ShareThreatToSquad(FSeam_EntityId Source, float Amount, FGameplayTag Tag)
{
	if (!HasAuthoritySafe() || !Source.IsValid())
	{
		return;
	}
	const float Shared = Amount * FMath::Clamp(SquadShareFraction, 0.f, 1.f);
	if (Shared <= 0.f)
	{
		return;
	}

	TArray<FSeam_EntityId> Squadmates;
	GetSquadmates(Squadmates);
	for (const FSeam_EntityId& MateId : Squadmates)
	{
		if (TScriptInterface<IAI_Threatened> MateThreat = ResolveThreatById(MateId))
		{
			MateThreat->AddThreat(Source, Shared, Tag);
		}
	}
}

void UAI_ThreatShareComponent::TransferThreat(FSeam_EntityId From, FSeam_EntityId To, float Fraction)
{
	if (!HasAuthoritySafe() || !From.IsValid() || !To.IsValid())
	{
		return;
	}
	IAI_Threatened* Own = ResolveOwnThreat();
	if (!Own)
	{
		return;
	}
	// Read the current threat from "From", move a fraction of it to "To". The seam exposes AddThreat
	// (negative reduces) and GetTopThreat; for an arbitrary source we read the precise value via the
	// owner's concrete threat component (same module type), which is the table that backs the seam.
	const float Frac = FMath::Clamp(Fraction, 0.f, 1.f);

	float FromValue = 0.f;
	if (AActor* Owner = GetOwner())
	{
		if (UAI_ThreatComponent* Concrete = Owner->FindComponentByClass<UAI_ThreatComponent>())
		{
			FromValue = Concrete->GetThreatFor(From);
		}
	}
	const float Move = FromValue * Frac;
	if (Move > 0.f)
	{
		Own->AddThreat(From, -Move, FGameplayTag());
		Own->AddThreat(To, Move, FGameplayTag());
	}
}

//~ Brain driving -------------------------------------------------------------------------------

void UAI_ThreatShareComponent::DriveBrainTarget()
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

	// Effective target: forced taunt wins, else our own top threat.
	FSeam_EntityId Effective = TauntForcedTarget;
	if (!Effective.IsValid())
	{
		if (IAI_Threatened* Own = ResolveOwnThreat())
		{
			Effective = Own->GetTopThreat();
		}
	}
	if (!Effective.IsValid())
	{
		return;
	}

	// Push into the owner's brain seam.
	if (Owner->GetClass()->ImplementsInterface(UAI_Brain::StaticClass()))
	{
		if (IAI_Brain* Brain = Cast<IAI_Brain>(Owner))
		{
			Brain->SetTargetEntity(Effective);
			return;
		}
	}
	if (UActorComponent* BrainComp = Owner->FindComponentByInterface(UAI_Brain::StaticClass()))
	{
		if (IAI_Brain* Brain = Cast<IAI_Brain>(BrainComp))
		{
			Brain->SetTargetEntity(Effective);
		}
	}
}

void UAI_ThreatShareComponent::HandleThreatChanged(const FDP_Message& /*Message*/)
{
	// Our own table changed: re-drive the brain target (authority only).
	if (HasAuthoritySafe() && bDriveBrainTarget)
	{
		DriveBrainTarget();
	}
}

//~ Resolution helpers --------------------------------------------------------------------------

IAI_Threatened* UAI_ThreatShareComponent::ResolveOwnThreat() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}
	if (Owner->GetClass()->ImplementsInterface(UAI_Threatened::StaticClass()))
	{
		return Cast<IAI_Threatened>(Owner);
	}
	if (UActorComponent* Comp = Owner->FindComponentByInterface(UAI_Threatened::StaticClass()))
	{
		return Cast<IAI_Threatened>(Comp);
	}
	return nullptr;
}

TScriptInterface<IAI_Threatened> UAI_ThreatShareComponent::ResolveThreatById(const FSeam_EntityId& EntityId) const
{
	TScriptInterface<IAI_Threatened> Result;
	AActor* Actor = ResolveActorById(EntityId);
	if (!Actor)
	{
		return Result;
	}
	if (Actor->GetClass()->ImplementsInterface(UAI_Threatened::StaticClass()))
	{
		Result.SetObject(Actor);
		Result.SetInterface(Cast<IAI_Threatened>(Actor));
		return Result;
	}
	if (UActorComponent* Comp = Actor->FindComponentByInterface(UAI_Threatened::StaticClass()))
	{
		Result.SetObject(Comp);
		Result.SetInterface(Cast<IAI_Threatened>(Comp));
	}
	return Result;
}

TScriptInterface<ISeam_TauntSink> UAI_ThreatShareComponent::ResolveTauntSinkById(const FSeam_EntityId& EntityId) const
{
	TScriptInterface<ISeam_TauntSink> Result;
	AActor* Actor = ResolveActorById(EntityId);
	if (!Actor)
	{
		return Result;
	}
	if (Actor->GetClass()->ImplementsInterface(USeam_TauntSink::StaticClass()))
	{
		Result.SetObject(Actor);
		Result.SetInterface(Cast<ISeam_TauntSink>(Actor));
		return Result;
	}
	if (UActorComponent* Comp = Actor->FindComponentByInterface(USeam_TauntSink::StaticClass()))
	{
		Result.SetObject(Comp);
		Result.SetInterface(Cast<ISeam_TauntSink>(Comp));
	}
	return Result;
}

void UAI_ThreatShareComponent::GetSquadmates(TArray<FSeam_EntityId>& OutMembers) const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(Owner))
	{
		if (UObject* SquadObj = Locator->ResolveService(AINativeTags::Service_AI_Squad))
		{
			if (SquadObj->GetClass()->ImplementsInterface(UAI_Squad::StaticClass()))
			{
				if (IAI_Squad* Squad = Cast<IAI_Squad>(SquadObj))
				{
					TArray<FSeam_EntityId> All;
					Squad->GetMembers(All);
					const FSeam_EntityId Me = GetOwnerEntityId();
					for (const FSeam_EntityId& Id : All)
					{
						if (Id.IsValid() && Id != Me)
						{
							OutMembers.Add(Id);
						}
					}
				}
			}
		}
	}
}

FSeam_EntityId UAI_ThreatShareComponent::GetOwnerEntityId() const
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

AActor* UAI_ThreatShareComponent::ResolveActorById(const FSeam_EntityId& EntityId) const
{
	if (!EntityId.IsValid())
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!Actor)
		{
			continue;
		}
		if (Actor->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
		{
			if (ISeam_EntityIdentity::Execute_GetEntityId(Actor) == EntityId)
			{
				return Actor;
			}
		}
		else if (UActorComponent* Comp = Actor->FindComponentByInterface(USeam_EntityIdentity::StaticClass()))
		{
			if (ISeam_EntityIdentity::Execute_GetEntityId(Comp) == EntityId)
			{
				return Actor;
			}
		}
	}
	return nullptr;
}

void UAI_ThreatShareComponent::OnRep_ForcedTarget(FSeam_EntityId /*PreviousTarget*/)
{
	// Cosmetic-only client reaction (e.g. UI taunt indicator). No authoritative work here.
}

bool UAI_ThreatShareComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
