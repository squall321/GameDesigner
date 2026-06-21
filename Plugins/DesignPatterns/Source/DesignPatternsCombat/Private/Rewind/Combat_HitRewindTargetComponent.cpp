// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Rewind/Combat_HitRewindTargetComponent.h"
#include "Health/Combat_HealthComponent.h"

#include "Identity/Seam_EntityIdentity.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "GameFramework/Actor.h"
#include "Components/PrimitiveComponent.h"

namespace
{
	/**
	 * Net-owned registration channel, resolved by NAME at runtime so Combat does not include any Net
	 * tag header (avoids a duplicate UE_DEFINE_GAMEPLAY_TAG of the same string in two modules). The Net
	 * lag-comp subsystem listens on this channel; the message's Instigator is the registering component,
	 * off which the subsystem resolves ISeam_HitRewindTarget. ErrorIfNotFound=false keeps it a clean
	 * no-op when the Net module (and thus the tag) is not loaded.
	 */
	FGameplayTag GetRegisterChannel()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Bus.Net.RewindRegister")), /*ErrorIfNotFound*/ false);
	}
	FGameplayTag GetDeregisterChannel()
	{
		return FGameplayTag::RequestGameplayTag(FName(TEXT("DP.Bus.Net.RewindDeregister")), /*ErrorIfNotFound*/ false);
	}
}

UCombat_HitRewindTargetComponent::UCombat_HitRewindTargetComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false); // no replicated state
}

void UCombat_HitRewindTargetComponent::BeginPlay()
{
	Super::BeginPlay();

	// Lag compensation is a SERVER concern; only the authority registers as a rewind target.
	if (HasAuthoritySafe())
	{
		FallbackEntityId = FSeam_EntityId::NewId();
		AnnounceRegistration(/*bRegister*/ true);
	}
}

void UCombat_HitRewindTargetComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (HasAuthoritySafe())
	{
		AnnounceRegistration(/*bRegister*/ false);
	}
	Super::EndPlay(EndPlayReason);
}

bool UCombat_HitRewindTargetComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

void UCombat_HitRewindTargetComponent::AnnounceRegistration(bool bRegister)
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	const FGameplayTag Channel = bRegister ? GetRegisterChannel() : GetDeregisterChannel();
	if (!Channel.IsValid())
	{
		// Net module / tag not present — nothing to register with. Clean no-op.
		return;
	}

	// Empty payload; the registration carries information via the Instigator (this component), off which
	// the Net subsystem resolves ISeam_HitRewindTarget. An empty FInstancedStruct is fine for the bus.
	FInstancedStruct Empty;
	Bus->BroadcastPayload(Channel, Empty, this);

	UE_LOG(LogDP, Verbose, TEXT("[HitRewind] %s announced %s."),
		*GetNameSafe(GetOwner()), bRegister ? TEXT("registration") : TEXT("deregistration"));
}

FSeam_EntityId UCombat_HitRewindTargetComponent::ResolveEntityId() const
{
	// Prefer the owner's stable EntityIdentity if present, so the lag-comp ring keys on the same id the
	// rest of the systems use; else fall back to our locally-minted id.
	if (AActor* Owner = GetOwner())
	{
		if (Owner->GetClass()->ImplementsInterface(USeam_EntityIdentity::StaticClass()))
		{
			const FSeam_EntityId Id = ISeam_EntityIdentity::Execute_GetEntityId(Owner);
			if (Id.IsValid())
			{
				return Id;
			}
		}
		if (UActorComponent* Comp = Owner->FindComponentByInterface(USeam_EntityIdentity::StaticClass()))
		{
			const FSeam_EntityId Id = ISeam_EntityIdentity::Execute_GetEntityId(Comp);
			if (Id.IsValid())
			{
				return Id;
			}
		}
	}
	return FallbackEntityId;
}

FSeam_EntityId UCombat_HitRewindTargetComponent::GetRewindEntityId_Implementation() const
{
	return ResolveEntityId();
}

bool UCombat_HitRewindTargetComponent::GetRewindBounds_Implementation(FBoxSphereBounds& OutBounds) const
{
	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return false;
	}

	// Use the actor's overall world bounds (includes collision primitives), padded slightly.
	FBox Box(ForceInit);
	bool bAnyValid = false;

	TArray<UPrimitiveComponent*> Primitives;
	Owner->GetComponents<UPrimitiveComponent>(Primitives);
	for (const UPrimitiveComponent* Prim : Primitives)
	{
		if (Prim && Prim->IsRegistered())
		{
			Box += Prim->Bounds.GetBox();
			bAnyValid = true;
		}
	}

	if (!bAnyValid)
	{
		// Defensive fallback: a small box around the actor location.
		Box = FBox::BuildAABB(Owner->GetActorLocation(), FVector(BoundsPadding + 50.f));
	}

	Box = Box.ExpandBy(FMath::Max(0.f, BoundsPadding));
	OutBounds = FBoxSphereBounds(Box);
	return OutBounds.SphereRadius > 0.f;
}

void UCombat_HitRewindTargetComponent::ApplyConfirmedHit_Implementation(AActor* Instigator, FGameplayTag /*DamageChannel*/, FSeam_NetValue Magnitude, FName /*HitBoneName*/)
{
	// Called ONLY by the lag-comp subsystem on the authority. Re-assert authority defensively.
	if (!HasAuthoritySafe())
	{
		return;
	}

	// The seam contract: magnitude must be a Float. Reject anything else rather than guessing.
	if (Magnitude.Type != ESeam_NetValueType::Float)
	{
		UE_LOG(LogDP, Warning, TEXT("[HitRewind] %s ApplyConfirmedHit ignored: magnitude not Float."),
			*GetNameSafe(GetOwner()));
		return;
	}

	const float Damage = static_cast<float>(Magnitude.FloatValue);
	if (Damage <= 0.f)
	{
		return;
	}

	if (UCombat_HealthComponent* Health = GetOwner()->FindComponentByClass<UCombat_HealthComponent>())
	{
		Health->ApplyDamage(Damage, Instigator);
	}
}
