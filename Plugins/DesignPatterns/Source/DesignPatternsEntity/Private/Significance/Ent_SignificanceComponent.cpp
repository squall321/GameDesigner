// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Significance/Ent_SignificanceComponent.h"
#include "Significance/Ent_SignificanceManagerSubsystem.h"
#include "DesignPatternsEntityTags.h"

#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Registry/Ent_EntityLifecycleMessage.h"
#include "Entity/Ent_EntityComponent.h"
#include "Identity/Seam_EntityIdentity.h"

#include "GameFramework/Actor.h"
#include "Engine/World.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UEnt_SignificanceComponent::UEnt_SignificanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Significance is purely local/cosmetic; never replicated.
	SetIsReplicatedByDefault(false);
}

void UEnt_SignificanceComponent::BeginPlay()
{
	Super::BeginPlay();
	if (UEnt_SignificanceManagerSubsystem* Mgr = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_SignificanceManagerSubsystem>(this))
	{
		Mgr->RegisterSignificanceComponent(this);
		bRegistered = true;
	}
}

void UEnt_SignificanceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (bRegistered)
	{
		if (UEnt_SignificanceManagerSubsystem* Mgr = FDP_SubsystemStatics::GetWorldSubsystem<UEnt_SignificanceManagerSubsystem>(this))
		{
			Mgr->UnregisterSignificanceComponent(this);
		}
		bRegistered = false;
	}
	Super::EndPlay(EndPlayReason);
}

void UEnt_SignificanceComponent::SetBucket(EEnt_SignificanceBucket NewBucket, float TickInterval, int32 DetailLevel)
{
	const bool bChanged = (NewBucket != CurrentBucket) || (DetailLevel != CurrentDetailLevel);

	CurrentBucket = NewBucket;
	CurrentDetailLevel = DetailLevel;

	if (bDriveOwnerTick)
	{
		if (AActor* Owner = GetOwner())
		{
			// Apply the band's tick interval to the owner's primary tick (0 = every frame).
			Owner->PrimaryActorTick.TickInterval = FMath::Max(0.f, TickInterval);
		}
	}

	if (bChanged)
	{
		OnSignificanceChanged.Broadcast(NewBucket, DetailLevel, TickInterval);

		// Local re-broadcast on the bus so non-owning local systems (audio/UI) can react.
		if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
		{
			FSeam_EntityId Id = FSeam_EntityId::Invalid();
			if (AActor* Owner = GetOwner())
			{
				if (UEnt_EntityComponent* Ent = Owner->FindComponentByClass<UEnt_EntityComponent>())
				{
					Id = ISeam_EntityIdentity::Execute_GetEntityId(Ent);
				}
			}
			const FEnt_EntityLifecycleMessage Message(EEnt_EntityLifecyclePhase::Registered, Id, FGameplayTag());
			FInstancedStruct Payload;
			Payload.InitializeAs<FEnt_EntityLifecycleMessage>(Message);
			Bus->BroadcastPayload(EntNativeTags::Bus_SignificanceChanged, Payload, GetOwner());
		}
	}
}
