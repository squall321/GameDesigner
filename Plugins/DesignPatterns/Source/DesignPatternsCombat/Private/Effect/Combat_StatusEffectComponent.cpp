// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Effect/Combat_StatusEffectComponent.h"
#include "Effect/Combat_StatusEffect.h"
#include "Hit/Combat_HitTypes.h"
#include "Combat_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

// FInstancedStruct is used directly here (bus payload). Version-gated for the 5.3-5.5 band.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

UCombat_StatusEffectComponent::UCombat_StatusEffectComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;

	// REPLICATION CONTRACT: enable replication so ActiveEffectTags travels to clients.
	SetIsReplicatedByDefault(true);
}

void UCombat_StatusEffectComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// Only the tag container replicates; per-instance timing stays server-side.
	DOREPLIFETIME(UCombat_StatusEffectComponent, ActiveEffectTags);
}

bool UCombat_StatusEffectComponent::HasAuthoritySafe() const
{
	const AActor* Owner = GetOwner();
	return Owner && Owner->HasAuthority();
}

float UCombat_StatusEffectComponent::GetWorldTimeSeconds() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.f;
}

void UCombat_StatusEffectComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// All effect timing/logic is authoritative.
	if (HasAuthoritySafe())
	{
		TickEffectsAuthority(DeltaTime);
	}
}

UCombat_StatusEffect* UCombat_StatusEffectComponent::ApplyEffect(TSubclassOf<UCombat_StatusEffect> EffectClass)
{
	// AUTHORITY GUARD: never mutate replicated state on a client.
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return nullptr;
	}

	if (!EffectClass)
	{
		return nullptr;
	}

	const UCombat_StatusEffect* CDO = EffectClass->GetDefaultObject<UCombat_StatusEffect>();
	const FGameplayTag EffectTag = CDO ? CDO->EffectTag : FGameplayTag();
	const float Now = GetWorldTimeSeconds();

	// Refresh an existing instance of the same tag instead of stacking duplicates.
	if (EffectTag.IsValid())
	{
		for (FCombat_ActiveStatus& Active : ActiveEffects)
		{
			if (Active.Effect && Active.Effect->EffectTag == EffectTag)
			{
				Active.ExpireTime = (Active.Effect->Duration > 0.f) ? Now + Active.Effect->Duration : 0.f;
				return Active.Effect;
			}
		}
	}

	// Instanced subobject owned by this component (valid world context + GC ownership).
	UCombat_StatusEffect* Effect = NewObject<UCombat_StatusEffect>(this, EffectClass);
	if (!Effect)
	{
		return nullptr;
	}

	FCombat_ActiveStatus Active;
	Active.Effect = Effect;
	Active.ExpireTime = (Effect->Duration > 0.f) ? Now + Effect->Duration : 0.f;
	Active.NextTickTime = (Effect->TickInterval > 0.f) ? Now + Effect->TickInterval : 0.f;
	ActiveEffects.Add(Active);

	AActor* Target = GetOwner();
	Effect->OnApply(Target);

	if (Effect->EffectTag.IsValid())
	{
		ActiveEffectTags.AddTag(Effect->EffectTag);
		OnStatusApplied.Broadcast(this, Effect->EffectTag);
		BroadcastStatusApplied(Effect->EffectTag);
	}

	return Effect;
}

void UCombat_StatusEffectComponent::RemoveEffect(UCombat_StatusEffect* Effect)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!Effect)
	{
		return;
	}

	for (int32 i = 0; i < ActiveEffects.Num(); ++i)
	{
		if (ActiveEffects[i].Effect == Effect)
		{
			RemoveAt(i, /*bExpiredNaturally*/ false);
			return;
		}
	}
}

void UCombat_StatusEffectComponent::RemoveEffectByTag(FGameplayTag EffectTag)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	for (int32 i = 0; i < ActiveEffects.Num(); ++i)
	{
		if (ActiveEffects[i].Effect && ActiveEffects[i].Effect->EffectTag == EffectTag)
		{
			RemoveAt(i, /*bExpiredNaturally*/ false);
			return;
		}
	}
}

void UCombat_StatusEffectComponent::TickEffectsAuthority(float DeltaTime)
{
	const float Now = GetWorldTimeSeconds();

	// Iterate backwards so RemoveAt can erase in place safely.
	for (int32 i = ActiveEffects.Num() - 1; i >= 0; --i)
	{
		FCombat_ActiveStatus& Active = ActiveEffects[i];
		UCombat_StatusEffect* Effect = Active.Effect;
		if (!Effect)
		{
			RemoveAt(i, /*bExpiredNaturally*/ true);
			continue;
		}

		// Periodic ticks.
		if (Effect->TickInterval > 0.f && Now >= Active.NextTickTime)
		{
			Effect->OnTick(GetOwner(), Effect->TickInterval);
			Active.NextTickTime = Now + Effect->TickInterval;
		}

		// Natural expiry (ExpireTime == 0 means infinite).
		if (Active.ExpireTime > 0.f && Now >= Active.ExpireTime)
		{
			RemoveAt(i, /*bExpiredNaturally*/ true);
		}
	}
}

void UCombat_StatusEffectComponent::RemoveAt(int32 Index, bool bExpiredNaturally)
{
	if (!ActiveEffects.IsValidIndex(Index))
	{
		return;
	}

	UCombat_StatusEffect* Effect = ActiveEffects[Index].Effect;
	const FGameplayTag EffectTag = Effect ? Effect->EffectTag : FGameplayTag();

	if (Effect)
	{
		Effect->OnRemove(GetOwner(), bExpiredNaturally);
	}

	ActiveEffects.RemoveAt(Index);

	// Clear the replicated tag only if no other active effect still carries it.
	if (EffectTag.IsValid())
	{
		bool bStillPresent = false;
		for (const FCombat_ActiveStatus& Other : ActiveEffects)
		{
			if (Other.Effect && Other.Effect->EffectTag == EffectTag)
			{
				bStillPresent = true;
				break;
			}
		}

		if (!bStillPresent)
		{
			ActiveEffectTags.RemoveTag(EffectTag);
			OnStatusRemoved.Broadcast(this, EffectTag);
			// Mirror the apply path: publish removal to the core bus for decoupled listeners
			// (scoring, AI, analytics). Fired on the authority; clients learn via OnRep delegates.
			BroadcastStatusRemoved(EffectTag);
		}
	}
}

void UCombat_StatusEffectComponent::BroadcastStatusApplied(FGameplayTag EffectTag)
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FCombat_StatusMessage Msg;
	Msg.Target = GetOwner();
	Msg.EffectTag = EffectTag;

	const FInstancedStruct Payload = FInstancedStruct::Make(Msg);
	Bus->BroadcastPayload(CombatNativeTags::Bus_Combat_StatusApplied, Payload, GetOwner());
}

void UCombat_StatusEffectComponent::BroadcastStatusRemoved(FGameplayTag EffectTag)
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	FCombat_StatusMessage Msg;
	Msg.Target = GetOwner();
	Msg.EffectTag = EffectTag;

	const FInstancedStruct Payload = FInstancedStruct::Make(Msg);
	Bus->BroadcastPayload(CombatNativeTags::Bus_Combat_StatusRemoved, Payload, GetOwner());
}

void UCombat_StatusEffectComponent::OnRep_ActiveEffectTags(FGameplayTagContainer OldTags)
{
	// Diff old vs new and fire the matching delegates locally on the client.
	for (const FGameplayTag& Tag : ActiveEffectTags)
	{
		if (!OldTags.HasTagExact(Tag))
		{
			OnStatusApplied.Broadcast(this, Tag);
		}
	}

	for (const FGameplayTag& Tag : OldTags)
	{
		if (!ActiveEffectTags.HasTagExact(Tag))
		{
			OnStatusRemoved.Broadcast(this, Tag);
		}
	}
}
