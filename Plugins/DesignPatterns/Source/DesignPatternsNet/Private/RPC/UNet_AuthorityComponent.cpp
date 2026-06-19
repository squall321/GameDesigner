// Copyright DesignPatterns plugin. All Rights Reserved.

#include "RPC/UNet_AuthorityComponent.h"
#include "Replication/UNet_NetUtilsLibrary.h"
#include "Core/DPLog.h"
#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UNet_AuthorityComponent::UNet_AuthorityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Required for any component holding Replicated/ReplicatedUsing UPROPERTYs (HARD RULE 6).
	SetIsReplicatedByDefault(true);

	// Configure the quantized counter: encode 0..255 with a Min of 0 in 8 bits.
	ConfirmedCount = FNet_RepInt(/*Value=*/0, /*Min=*/0, /*NumBits=*/8);
}

void UNet_AuthorityComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	// Tiny, must-be-synced surface only (HARD RULE 9). The full ServerActionCounts map stays server-side.
	DOREPLIFETIME(UNet_AuthorityComponent, LastConfirmedAction);
	DOREPLIFETIME(UNet_AuthorityComponent, ConfirmedCount);
}

void UNet_AuthorityComponent::RequestAction(FGameplayTag ActionTag)
{
	if (!ActionTag.IsValid())
	{
		UE_LOG(LogDP, Warning, TEXT("UNet_AuthorityComponent::RequestAction: invalid tag ignored."));
		return;
	}

	// On the authority we can apply directly; otherwise forward to the server. This is the
	// canonical "client asks, server decides" split — a client never mutates replicated state.
	if (UNet_NetUtilsLibrary::HasAuthority(GetOwner()))
	{
		ApplyActionAuthoritative(ActionTag);
	}
	else
	{
		ServerRequestAction(ActionTag);
	}
}

bool UNet_AuthorityComponent::ServerRequestAction_Validate(FGameplayTag ActionTag)
{
	// Cheap anti-cheat gate. Reject obviously-malformed requests before they reach _Implementation.
	// Returning false here disconnects the malicious client, so only reject things a legit client
	// could never legitimately send.
	return ActionTag.IsValid();
}

void UNet_AuthorityComponent::ServerRequestAction_Implementation(FGameplayTag ActionTag)
{
	// Runs on the server. Authority is guaranteed by the RPC routing, but we still go through the
	// shared authoritative apply (which re-checks authority) for a single source of truth.
	ApplyActionAuthoritative(ActionTag);
}

void UNet_AuthorityComponent::ApplyActionAuthoritative(FGameplayTag ActionTag)
{
	// HARD RULE 6: guard replicated-state mutation at the TOP. Fail-closed on non-authority.
	if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UNet_AuthorityComponent::ApplyActionAuthoritative")))
	{
		return;
	}

	// Game-specific validation seam (server side).
	if (!CanServerApplyAction(ActionTag))
	{
		UE_LOG(LogDP, Verbose, TEXT("UNet_AuthorityComponent: server rejected action '%s'."), *ActionTag.ToString());
		return;
	}

	// --- Apply authoritative state ---
	int32& Count = ServerActionCounts.FindOrAdd(ActionTag);
	++Count;

	// Update the small replicated surface. ConfirmedCount.Set clamps into its encodable range.
	LastConfirmedAction = ActionTag;
	ConfirmedCount.Set(Count);

	// Push-model dirty marks so the OnReps fire on clients.
	MARK_PROPERTY_DIRTY_FROM_NAME(UNet_AuthorityComponent, LastConfirmedAction, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UNet_AuthorityComponent, ConfirmedCount, this);

	// Server fires its own local notifications immediately (clients get them via OnRep).
	OnActionCounterChanged.Broadcast(ActionTag, ConfirmedCount.Get());

	UE_LOG(LogDP, Verbose, TEXT("UNet_AuthorityComponent: confirmed '%s' (count=%d) on %s."),
		*ActionTag.ToString(), Count, *UNet_NetUtilsLibrary::DescribeNetContext(GetOwner()));

	// Cosmetic feedback to every machine. Carries no authoritative state.
	MulticastActionConfirmed(ActionTag);
}

void UNet_AuthorityComponent::MulticastActionConfirmed_Implementation(FGameplayTag ActionTag)
{
	// Runs on server + all clients. Pure cosmetic broadcast (sound/FX hook point).
	OnActionConfirmed.Broadcast(ActionTag);
}

bool UNet_AuthorityComponent::CanServerApplyAction_Implementation(FGameplayTag ActionTag) const
{
	// Default policy: accept any valid tag. Designers override to add cooldowns/costs/gating.
	return ActionTag.IsValid();
}

int32 UNet_AuthorityComponent::GetActionCount(FGameplayTag ActionTag) const
{
	// On the authority, answer from the full map. On clients, fall back to the replicated surface
	// (only the last-confirmed action's count is known there).
	if (const int32* Found = ServerActionCounts.Find(ActionTag))
	{
		return *Found;
	}
	if (ActionTag.IsValid() && ActionTag == LastConfirmedAction)
	{
		return ConfirmedCount.Get();
	}
	return 0;
}

void UNet_AuthorityComponent::OnRep_LastConfirmedAction()
{
	// Client learns which action was last confirmed; pair with OnRep_ConfirmedCount for the count.
	UE_LOG(LogDP, Verbose, TEXT("UNet_AuthorityComponent: OnRep LastConfirmedAction='%s'."),
		*LastConfirmedAction.ToString());
}

void UNet_AuthorityComponent::OnRep_ConfirmedCount()
{
	// Client-side counter update — drive UI from here.
	OnActionCounterChanged.Broadcast(LastConfirmedAction, ConfirmedCount.Get());
}
