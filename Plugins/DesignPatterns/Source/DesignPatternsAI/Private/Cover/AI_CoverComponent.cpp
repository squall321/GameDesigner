// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Cover/AI_CoverComponent.h"
#include "Cover/AI_CoverSubsystem.h"
#include "Cover/AI_CoverPoint.h"
#include "Perception/AI_PerceptionComponent.h"
#include "Settings/AI_DeveloperSettings.h"
#include "Tactical/AI_TacticalBusPayloads.h"
#include "DesignPatternsAINativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "FSM/DPBlackboard.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#include "Seams/AI_Threatened.h"
#include "Identity/Seam_EntityIdentity.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

// FInstancedStruct lives in StructUtils on 5.3/5.4, merged into CoreUObject on 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UAI_CoverComponent::UAI_CoverComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UAI_CoverComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UAI_CoverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Free our claim so we never leak a held cover point when destroyed.
	if (HasAuthoritySafe())
	{
		ReleaseCover();
	}
	Super::EndPlay(EndPlayReason);
}

void UAI_CoverComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UAI_CoverComponent, ClaimedCoverLocation);
	DOREPLIFETIME(UAI_CoverComponent, bPeeking);
}

//~ Acquire / release ---------------------------------------------------------------------------

bool UAI_CoverComponent::AcquireCover()
{
	if (!HasAuthoritySafe())
	{
		return false;
	}
	UAI_CoverSubsystem* Cover = ResolveCoverSubsystem();
	AActor* Owner = GetOwner();
	if (!Cover || !Owner)
	{
		return false;
	}

	const FVector Origin = Owner->GetActorLocation();
	const FVector Threat = ResolveThreatLocation();

	// SearchRadius <= 0 means "use the project default" (and FindBestCoverPoint treats <= 0 as unbounded).
	float EffectiveRadius = SearchRadius;
	if (EffectiveRadius <= 0.f)
	{
		if (const UAI_DeveloperSettings* Settings = UAI_DeveloperSettings::Get())
		{
			EffectiveRadius = Settings->DefaultCoverSearchRadius;
		}
	}

	AAI_CoverPoint* Point = Cover->FindBestCoverPoint(Origin, Threat, EffectiveRadius);
	if (!Point)
	{
		return false;
	}

	const FSeam_EntityId Me = GetOwnerEntityId();
	if (!Point->TryClaim(Me))
	{
		return false;
	}

	// Release any prior point now we hold a new one.
	if (HeldCoverId.IsValid() && HeldCoverId != Point->GetCoverId())
	{
		if (AAI_CoverPoint* Prior = Cover->FindCoverPointById(HeldCoverId))
		{
			Prior->Release(Me);
		}
	}

	HeldCoverId = Point->GetCoverId();
	const FVector CoverLoc = Point->GetActorLocation();
	ClaimedCoverLocation = CoverLoc;

	PushCoverToBlackboard(CoverLoc, /*bInCover=*/true);
	BroadcastCoverOnBus(HeldCoverId, Point->GetCoverTypeTag(), /*bClaimed=*/true, CoverLoc);
	OnCoverStateChanged.Broadcast(true);
	return true;
}

void UAI_CoverComponent::ReleaseCover()
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	if (!HeldCoverId.IsValid())
	{
		return;
	}
	const FSeam_EntityId Me = GetOwnerEntityId();
	FGameplayTag CoverType;
	FVector CoverLoc = ClaimedCoverLocation;
	if (UAI_CoverSubsystem* Cover = ResolveCoverSubsystem())
	{
		if (AAI_CoverPoint* Point = Cover->FindCoverPointById(HeldCoverId))
		{
			CoverType = Point->GetCoverTypeTag();
			CoverLoc = Point->GetActorLocation();
			Point->Release(Me);
		}
	}

	const FSeam_EntityId Released = HeldCoverId;
	HeldCoverId = FSeam_EntityId::Invalid();
	ClaimedCoverLocation = FVector::ZeroVector;
	bPeeking = false;

	PushCoverToBlackboard(FVector::ZeroVector, /*bInCover=*/false);
	BroadcastCoverOnBus(Released, CoverType, /*bClaimed=*/false, CoverLoc);
	OnCoverStateChanged.Broadcast(false);
}

void UAI_CoverComponent::BeginPeek()
{
	if (!HasAuthoritySafe() || !HeldCoverId.IsValid())
	{
		return;
	}
	bPeeking = true;
}

void UAI_CoverComponent::EndPeek()
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	bPeeking = false;
}

//~ Resolution helpers --------------------------------------------------------------------------

UAI_CoverSubsystem* UAI_CoverComponent::ResolveCoverSubsystem() const
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UAI_CoverSubsystem>(this);
}

IDP_BlackboardProvider* UAI_CoverComponent::ResolveBlackboardProvider() const
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

FVector UAI_CoverComponent::ResolveThreatLocation() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return FVector::ZeroVector;
	}

	// 1) Perception last-known target location (strongest percept), if any.
	if (UAI_PerceptionComponent* Perception = Owner->FindComponentByClass<UAI_PerceptionComponent>())
	{
		FAI_Percept Percept;
		if (Perception->GetStrongestPercept(FGameplayTag(), Percept))
		{
			return Percept.LastKnownLocation;
		}
	}

	// 2) Threat table top threat → resolve its actor location. The threat seam may be on the owner itself
	// or one of its components.
	IAI_Threatened* Threat = nullptr;
	if (Owner->GetClass()->ImplementsInterface(UAI_Threatened::StaticClass()))
	{
		Threat = Cast<IAI_Threatened>(Owner);
	}
	else if (UActorComponent* ThreatComp = Owner->FindComponentByInterface(UAI_Threatened::StaticClass()))
	{
		Threat = Cast<IAI_Threatened>(ThreatComp);
	}
	{
		if (Threat)
		{
			const FSeam_EntityId Top = Threat->GetTopThreat();
			if (Top.IsValid())
			{
				if (UWorld* World = GetWorld())
				{
					for (TActorIterator<AActor> It(World); It; ++It)
					{
						AActor* Candidate = *It;
						if (!Candidate)
						{
							continue;
						}
						if (Candidate->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
						{
							if (ISeam_EntityIdentity::Execute_GetEntityId(Candidate) == Top)
							{
								return Candidate->GetActorLocation();
							}
						}
					}
				}
			}
		}
	}

	// 3) Fallback: a point in front of the owner so cover at least faces somewhere sensible.
	return Owner->GetActorLocation() + Owner->GetActorForwardVector() * FMath::Max(1.f, FallbackThreatForwardDistance);
}

FSeam_EntityId UAI_CoverComponent::GetOwnerEntityId() const
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

void UAI_CoverComponent::PushCoverToBlackboard(const FVector& Location, bool bInCover)
{
	if (!HasAuthoritySafe())
	{
		return;
	}
	if (IDP_BlackboardProvider* Board = ResolveBlackboardProvider())
	{
		if (bInCover)
		{
			Board->SetVector(BlackboardKey_CoverLocation, Location);
		}
		else
		{
			Board->ClearKey(BlackboardKey_CoverLocation);
		}
		Board->SetBool(BlackboardKey_InCover, bInCover);
	}
}

void UAI_CoverComponent::BroadcastCoverOnBus(const FSeam_EntityId& CoverId, FGameplayTag CoverTypeTag, bool bClaimed, const FVector& Location) const
{
	if (!bBroadcastOnBus)
	{
		return;
	}
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		FAI_CoverClaimPayload Payload;
		Payload.AgentId = GetOwnerEntityId();
		Payload.CoverId = CoverId;
		Payload.CoverTypeTag = CoverTypeTag;
		Payload.bClaimed = bClaimed;
		Payload.CoverLocation = Location;
		Bus->BroadcastPayload(AINativeTags::Bus_AI_Cover_Claimed, FInstancedStruct::Make(Payload), this);
	}
}

void UAI_CoverComponent::OnRep_CoverState()
{
	// Cosmetic client-side reaction: surface the cover-state change so peek/lean cosmetics can play.
	OnCoverStateChanged.Broadcast(!ClaimedCoverLocation.IsZero());
}

bool UAI_CoverComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}
