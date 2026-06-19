// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Action/DPGameplayActionComponent.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

DECLARE_CYCLE_STAT(TEXT("Action Component Activate"), STAT_DP_ActionComponentActivate, STATGROUP_DesignPatterns);
DECLARE_DWORD_COUNTER_STAT(TEXT("Actions Activated"), STAT_DP_ActionsActivated, STATGROUP_DesignPatterns);

UDP_GameplayActionComponent::UDP_GameplayActionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UDP_GameplayActionComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Only the small, must-be-synced surface replicates (HARD RULE 9).
	DOREPLIFETIME(UDP_GameplayActionComponent, GrantedActionTags);
	DOREPLIFETIME(UDP_GameplayActionComponent, OwnedTags);
}

void UDP_GameplayActionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// End any active actions cleanly so subclasses can release timers/effects.
	for (FDP_ActionSpec& Spec : Specs)
	{
		if (Spec.bIsActive && Spec.Action)
		{
			FDP_ActionActivationData Data;
			Data.SourceComponent = this;
			Spec.Action->EndAction(Data, /*bWasCancelled=*/true);
			Spec.bIsActive = false;
		}
	}
	Specs.Reset();
	Super::EndPlay(EndPlayReason);
}

float UDP_GameplayActionComponent::GetWorldTimeSeconds() const
{
	if (const UWorld* World = GetWorld())
	{
		return World->GetTimeSeconds();
	}
	return 0.f;
}

FDP_ActionSpec* UDP_GameplayActionComponent::FindSpec(const FDP_ActionSpecHandle& Handle)
{
	return Specs.FindByPredicate([&Handle](const FDP_ActionSpec& S){ return S.Handle == Handle; });
}

const FDP_ActionSpec* UDP_GameplayActionComponent::FindSpec(const FDP_ActionSpecHandle& Handle) const
{
	return Specs.FindByPredicate([&Handle](const FDP_ActionSpec& S){ return S.Handle == Handle; });
}

bool UDP_GameplayActionComponent::HasAuthorityToMutate() const
{
	const AActor* OwnerActor = GetOwner();
	return OwnerActor && OwnerActor->HasAuthority();
}

void UDP_GameplayActionComponent::RefreshGrantedActionTags()
{
	GrantedActionTags.Reset();
	for (const FDP_ActionSpec& Spec : Specs)
	{
		if (Spec.Action && Spec.Action->ActionTag.IsValid())
		{
			GrantedActionTags.AddTag(Spec.Action->ActionTag);
		}
	}
	// Notify the replication system the container changed (server side).
	if (GetOwner() && GetOwner()->HasAuthority())
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UDP_GameplayActionComponent, GrantedActionTags, this);
	}
}

FDP_ActionSpecHandle UDP_GameplayActionComponent::GrantAction(TSubclassOf<UDP_GameplayActionLite> ActionClass)
{
	// Specs is server-authoritative and feeds the replicated GrantedActionTags. A client must not
	// mutate it (this is BlueprintCallable) or its local view diverges from the server.
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDPAction, Warning, TEXT("GrantAction ignored on non-authority — grant actions on the server."));
		return FDP_ActionSpecHandle();
	}

	if (!ActionClass)
	{
		UE_LOG(LogDPAction, Warning, TEXT("GrantAction: null action class."));
		return FDP_ActionSpecHandle();
	}
	if (ActionClass->HasAnyClassFlags(CLASS_Abstract))
	{
		UE_LOG(LogDPAction, Warning, TEXT("GrantAction: action class '%s' is abstract."), *ActionClass->GetName());
		return FDP_ActionSpecHandle();
	}

	// Instanced subobject: Outer = this component so it is GC-owned and has a world context.
	UDP_GameplayActionLite* Action = NewObject<UDP_GameplayActionLite>(this, ActionClass);
	if (!Action)
	{
		UE_LOG(LogDPAction, Error, TEXT("GrantAction: failed to instantiate '%s'."), *ActionClass->GetName());
		return FDP_ActionSpecHandle();
	}

	FDP_ActionSpec Spec;
	Spec.Handle.GenerateNewId();
	Spec.Action = Action;
	Spec.CooldownEndTime = 0.f;
	Spec.bIsActive = false;

	Specs.Add(Spec);
	RefreshGrantedActionTags();

	UE_LOG(LogDPAction, Verbose, TEXT("Granted action '%s' (%s)."),
		*Action->ActionTag.ToString(), *Spec.Handle.ToString());
	return Spec.Handle;
}

bool UDP_GameplayActionComponent::RemoveAction(FDP_ActionSpecHandle Handle)
{
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDPAction, Warning, TEXT("RemoveAction ignored on non-authority."));
		return false;
	}

	const int32 Index = Specs.IndexOfByPredicate([&Handle](const FDP_ActionSpec& S){ return S.Handle == Handle; });
	if (Index == INDEX_NONE)
	{
		return false;
	}

	FDP_ActionSpec& Spec = Specs[Index];
	if (Spec.bIsActive && Spec.Action)
	{
		FDP_ActionActivationData Data;
		Data.SourceComponent = this;
		Spec.Action->EndAction(Data, /*bWasCancelled=*/true);
	}
	Specs.RemoveAt(Index);
	RefreshGrantedActionTags();
	return true;
}

bool UDP_GameplayActionComponent::RemoveActionByTag(FGameplayTag ActionTag)
{
	if (!HasAuthorityToMutate())
	{
		UE_LOG(LogDPAction, Warning, TEXT("RemoveActionByTag ignored on non-authority."));
		return false;
	}

	const FDP_ActionSpec* Found = Specs.FindByPredicate([&ActionTag](const FDP_ActionSpec& S)
	{
		return S.Action && S.Action->ActionTag == ActionTag;
	});
	return Found ? RemoveAction(Found->Handle) : false;
}

bool UDP_GameplayActionComponent::ActivateAction(FDP_ActionSpecHandle Handle, const FDP_ActionActivationData& Data)
{
	SCOPE_CYCLE_COUNTER(STAT_DP_ActionComponentActivate);

	FDP_ActionSpec* Spec = FindSpec(Handle);
	if (!Spec || !Spec->Action)
	{
		UE_LOG(LogDPAction, Warning, TEXT("ActivateAction: no granted action for handle %s."), *Handle.ToString());
		return false;
	}

	// Cooldown gate (world-time based).
	const float Now = GetWorldTimeSeconds();
	if (Spec->CooldownEndTime > Now)
	{
		UE_LOG(LogDPAction, Verbose, TEXT("ActivateAction '%s' on cooldown (%.2fs left)."),
			*Spec->Action->ActionTag.ToString(), Spec->CooldownEndTime - Now);
		return false;
	}

	// Ensure the activation data carries this component as the source.
	FDP_ActionActivationData EffectiveData = Data;
	EffectiveData.SourceComponent = this;

	if (!Spec->Action->CanActivate(EffectiveData))
	{
		return false;
	}

	if (!Spec->Action->Activate(EffectiveData))
	{
		// Action declined to start; no cooldown applied.
		return false;
	}

	Spec->bIsActive = true;
	if (Spec->Action->CooldownDuration > 0.f)
	{
		Spec->CooldownEndTime = Now + Spec->Action->CooldownDuration;
	}

	INC_DWORD_STAT(STAT_DP_ActionsActivated);
	OnActionActivated.Broadcast(Spec->Action->ActionTag, Spec->Handle);
	return true;
}

bool UDP_GameplayActionComponent::ActivateActionByTag(FGameplayTag ActionTag, const FDP_ActionActivationData& Data)
{
	const FDP_ActionSpec* Found = Specs.FindByPredicate([&ActionTag](const FDP_ActionSpec& S)
	{
		return S.Action && S.Action->ActionTag == ActionTag;
	});
	if (!Found)
	{
		UE_LOG(LogDPAction, Verbose, TEXT("ActivateActionByTag: no action with tag '%s'."), *ActionTag.ToString());
		return false;
	}
	return ActivateAction(Found->Handle, Data);
}

void UDP_GameplayActionComponent::EndAction(FDP_ActionSpecHandle Handle, const FDP_ActionActivationData& Data, bool bWasCancelled)
{
	FDP_ActionSpec* Spec = FindSpec(Handle);
	if (!Spec || !Spec->Action)
	{
		return;
	}
	if (!Spec->bIsActive)
	{
		return;
	}

	FDP_ActionActivationData EffectiveData = Data;
	EffectiveData.SourceComponent = this;

	Spec->Action->EndAction(EffectiveData, bWasCancelled);
	Spec->bIsActive = false;
	OnActionEnded.Broadcast(Spec->Action->ActionTag, bWasCancelled);
}

bool UDP_GameplayActionComponent::HasActionWithTag(FGameplayTag ActionTag) const
{
	// Use the replicated tag list so this answers correctly on clients too.
	return GrantedActionTags.HasTag(ActionTag);
}

bool UDP_GameplayActionComponent::IsActionReady(FDP_ActionSpecHandle Handle) const
{
	const FDP_ActionSpec* Spec = FindSpec(Handle);
	if (!Spec)
	{
		return false;
	}
	return Spec->CooldownEndTime <= GetWorldTimeSeconds();
}

float UDP_GameplayActionComponent::GetActionCooldownRemaining(FDP_ActionSpecHandle Handle) const
{
	const FDP_ActionSpec* Spec = FindSpec(Handle);
	if (!Spec)
	{
		return 0.f;
	}
	return FMath::Max(0.f, Spec->CooldownEndTime - GetWorldTimeSeconds());
}

void UDP_GameplayActionComponent::AddOwnedTag(FGameplayTag Tag)
{
	// OwnedTags is replicated. Guard BEFORE mutating so a client never edits its local copy
	// (which would silently diverge until the next server replication overwrites it).
	if (!HasAuthorityToMutate())
	{
		return;
	}
	if (!Tag.IsValid())
	{
		return;
	}
	OwnedTags.AddTag(Tag);
	MARK_PROPERTY_DIRTY_FROM_NAME(UDP_GameplayActionComponent, OwnedTags, this);
}

void UDP_GameplayActionComponent::RemoveOwnedTag(FGameplayTag Tag)
{
	if (!HasAuthorityToMutate())
	{
		return;
	}
	if (OwnedTags.RemoveTag(Tag))
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UDP_GameplayActionComponent, OwnedTags, this);
	}
}

void UDP_GameplayActionComponent::OnRep_GrantedActionTags()
{
	// Clients learn the granted-tag set here; UI bound to HasActionWithTag updates accordingly.
	UE_LOG(LogDPAction, Verbose, TEXT("OnRep_GrantedActionTags: %d tags."), GrantedActionTags.Num());
}
