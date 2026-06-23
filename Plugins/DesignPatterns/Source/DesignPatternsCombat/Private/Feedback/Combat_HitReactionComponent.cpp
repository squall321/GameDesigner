// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Feedback/Combat_HitReactionComponent.h"
#include "Hit/Combat_DamagePipelineComponent.h"
#include "Pipeline/Combat_DamageContext.h"
#include "Hit/Combat_HitTypes.h"
#include "Combat_DeepNativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "MessageBus/DPMessage.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "TimerManager.h"

UCombat_HitReactionComponent::UCombat_HitReactionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Cosmetic only — NEVER replicated.
	SetIsReplicatedByDefault(false);
}

void UCombat_HitReactionComponent::BeginPlay()
{
	Super::BeginPlay();

	// Bind the server-side pipeline delegate when a pipeline component exists on the owner.
	if (const AActor* Owner = GetOwner())
	{
		if (UCombat_DamagePipelineComponent* Pipeline = Owner->FindComponentByClass<UCombat_DamagePipelineComponent>())
		{
			Pipeline->OnDamageResolved.AddDynamic(this, &UCombat_HitReactionComponent::HandlePipelineResolved);
		}
	}

	// Clients (and everyone) react via the local bus message produced from replicated state.
	if (bListenOnBus)
	{
		SubscribeBus();
	}
}

void UCombat_HitReactionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (const AActor* Owner = GetOwner())
	{
		if (UCombat_DamagePipelineComponent* Pipeline = Owner->FindComponentByClass<UCombat_DamagePipelineComponent>())
		{
			Pipeline->OnDamageResolved.RemoveDynamic(this, &UCombat_HitReactionComponent::HandlePipelineResolved);
		}
	}

	UnsubscribeBus();

	// Cancel any pending hitstop restore so the timer manager never calls back into a dead component.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HitstopTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UCombat_HitReactionComponent::SubscribeBus()
{
	UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		return;
	}

	TWeakObjectPtr<UCombat_HitReactionComponent> WeakThis(this);
	BusHandle = Bus->ListenNative(
		CombatDeepNativeTags::Bus_Combat_HitFeedback,
		[WeakThis](const FDP_Message& Message)
		{
			UCombat_HitReactionComponent* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}

			// Only react to feedback targeting our own owner (the message carries the victim as HitActor).
			if (!Message.Payload.IsValid())
			{
				return;
			}
			const FCombat_HitResult* Feedback = Message.Payload.GetPtr<FCombat_HitResult>();
			if (!Feedback)
			{
				return;
			}
			if (Feedback->HitActor.Get() != Self->GetOwner())
			{
				return;
			}

			// The reaction classification rode in on SourceTag; impact in ImpactPoint.
			const FGameplayTag ReactionTag = Feedback->SourceTag;
			const bool bCrit = (ReactionTag == CombatDeepNativeTags::Reaction_Critical);
			const bool bWeak = (ReactionTag == CombatDeepNativeTags::Reaction_Weakpoint);
			const bool bStagger = (ReactionTag == CombatDeepNativeTags::Reaction_Stagger);
			Self->PlayReaction(ReactionTag, Feedback->ImpactPoint, bCrit, bWeak, bStagger);
		},
		this,
		EDP_MessageMatch::ExactOrChild);
}

void UCombat_HitReactionComponent::UnsubscribeBus()
{
	if (!BusHandle.IsValid())
	{
		return;
	}
	if (UDP_MessageBusSubsystem* Bus = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this))
	{
		Bus->StopListening(BusHandle);
	}
	BusHandle = FDP_ListenerHandle();
}

void UCombat_HitReactionComponent::HandlePipelineResolved(UCombat_DamagePipelineComponent* /*Component*/, const FCombat_DamageResult& Result)
{
	// Server-authoritative path. On a listen-server the owner is also a player, so we still want the
	// reaction here; clients get it via the bus. Guard against double-fire on the server by only
	// reacting here when there is no bus subscription (bListenOnBus false) OR when we are the server
	// host with no remote — simplest is to always react here AND on bus, but de-dupe by reaction tag is
	// overkill; the bus message for our own owner is what a dedicated server never plays (no rendering),
	// so reacting in both places is harmless. We react here for the standalone/listen-server case.
	if (Result.Victim.Get() != GetOwner())
	{
		return;
	}
	PlayReaction(Result.ReactionTag, Result.ImpactPoint, Result.bIsCritical, Result.bIsWeakpoint, Result.bStaggered);
}

void UCombat_HitReactionComponent::PlayReaction(FGameplayTag ReactionTag, const FVector& ImpactPoint, bool bCritical, bool bWeakpoint, bool bStagger)
{
	const bool bSalient = bCritical || bWeakpoint || bStagger;
	ApplyHitstop(bSalient);

	OnReactionPlayed.Broadcast(ReactionTag, ImpactPoint);
	ReceiveReaction(ReactionTag, ImpactPoint, bCritical, bWeakpoint);

	UE_LOG(LogDP, VeryVerbose, TEXT("[HitReaction] %s reaction '%s'."),
		*GetNameSafe(GetOwner()), *ReactionTag.ToString());
}

void UCombat_HitReactionComponent::ApplyHitstop(bool bSalient)
{
	if (HitstopSeconds <= 0.f)
	{
		return;
	}

	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (!Owner || !World)
	{
		return;
	}

	// Local-only: dilate the OWNER's time briefly. This is purely cosmetic and never touches the
	// authoritative simulation clock (we use actor-local CustomTimeDilation, not global dilation).
	Owner->CustomTimeDilation = FMath::Clamp(HitstopDilation, 0.f, 1.f);

	const float Duration = HitstopSeconds * (bSalient ? FMath::Max(1.f, SalientHitstopMultiplier) : 1.f);

	World->GetTimerManager().SetTimer(HitstopTimerHandle, this, &UCombat_HitReactionComponent::RestoreTimeDilation, Duration, false);
}

void UCombat_HitReactionComponent::RestoreTimeDilation()
{
	if (AActor* Owner = GetOwner())
	{
		Owner->CustomTimeDilation = 1.f;
	}
}
