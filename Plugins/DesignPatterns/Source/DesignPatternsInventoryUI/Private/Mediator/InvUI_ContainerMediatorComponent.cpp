// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Mediator/InvUI_ContainerMediatorComponent.h"

#include "Registry/InvUI_ContainerRegistry.h"
#include "Seam/InvUI_ContainerAccess.h"
#include "InvUI_NativeTags.h"

#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

UInvUI_ContainerMediatorComponent::UInvUI_ContainerMediatorComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Replicated so its existence/owner is known on all relevant connections; it carries no
	// authoritative state of its own (that lives on the container backends), so there are no
	// replicated UPROPERTYs and thus no GetLifetimeReplicatedProps to implement.
	SetIsReplicatedByDefault(true);
}

bool UInvUI_ContainerMediatorComponent::HasAuthorityToMutate() const
{
	const AActor* Owner = GetOwner();
	return Owner != nullptr && Owner->HasAuthority();
}

UInvUI_ContainerRegistry* UInvUI_ContainerMediatorComponent::GetRegistry() const
{
	return UInvUI_ContainerRegistry::Get(this);
}

void UInvUI_ContainerMediatorComponent::RequestMoveByIdentity(
	FInvUI_ContainerInstanceId FromContainer, FGameplayTag FromSlot,
	FInvUI_ContainerInstanceId ToContainer, FGameplayTag ToSlot,
	int32 Count)
{
	// Reject obviously-malformed requests on the client before spending a reliable RPC.
	if (!FromContainer.IsValid() || !FromSlot.IsValid() || !ToContainer.IsValid() || Count < 0)
	{
		BroadcastResult(EInvUI_MoveResult::Rejected, FromContainer, FromSlot, ToContainer, ToSlot, Count);
		return;
	}

	if (HasAuthorityToMutate())
	{
		// Listen-server / standalone: validate + dispatch directly, then report locally.
		const EInvUI_MoveResult Result =
			ValidateAndDispatchMove(FromContainer, FromSlot, ToContainer, ToSlot, Count);
		BroadcastResult(Result, FromContainer, FromSlot, ToContainer, ToSlot, Count);
		return;
	}

	// Pure client: forward to the server. The server re-resolves and re-validates everything.
	Server_MoveByIdentity(FromContainer, FromSlot, ToContainer, ToSlot, Count);
}

bool UInvUI_ContainerMediatorComponent::Server_MoveByIdentity_Validate(
	FInvUI_ContainerInstanceId FromContainer, FGameplayTag FromSlot,
	FInvUI_ContainerInstanceId ToContainer, FGameplayTag ToSlot,
	int32 Count)
{
	// Structural validation only — never trust the values for resolution; the server re-resolves.
	// A negative count or invalid ids are protocol violations and fail validation (drops the conn).
	return FromContainer.IsValid() && FromSlot.IsValid() && ToContainer.IsValid() && Count >= 0;
}

void UInvUI_ContainerMediatorComponent::Server_MoveByIdentity_Implementation(
	FInvUI_ContainerInstanceId FromContainer, FGameplayTag FromSlot,
	FInvUI_ContainerInstanceId ToContainer, FGameplayTag ToSlot,
	int32 Count)
{
	// AUTHORITY GUARD at the top: a mis-routed RPC on a non-authority is a no-op.
	if (!HasAuthorityToMutate())
	{
		return;
	}

	const EInvUI_MoveResult Result =
		ValidateAndDispatchMove(FromContainer, FromSlot, ToContainer, ToSlot, Count);

	// Broadcast server-side (for server listeners) and mirror the outcome to the requesting client.
	BroadcastResult(Result, FromContainer, FromSlot, ToContainer, ToSlot, Count);
	Client_ReportMoveResult(Result, FromContainer, FromSlot, ToContainer, ToSlot, Count);
}

bool UInvUI_ContainerMediatorComponent::PassesAccessGate(
	const TScriptInterface<IInvUI_ItemContainer>& Container, AActor* Requestor) const
{
	UObject* Obj = Container.GetObject();
	if (!Obj)
	{
		return false;
	}

	// The access gate is OPTIONAL: a container that does not implement it is unconditionally
	// reachable (the router still performed authority + identity re-derivation).
	if (Obj->GetClass()->ImplementsInterface(UInvUI_ContainerAccess::StaticClass()))
	{
		return IInvUI_ContainerAccess::Execute_CanAccess(Obj, Requestor);
	}
	return true;
}

EInvUI_MoveResult UInvUI_ContainerMediatorComponent::ValidateAndDispatchMove(
	const FInvUI_ContainerInstanceId& FromContainer, const FGameplayTag& FromSlot,
	const FInvUI_ContainerInstanceId& ToContainer, const FGameplayTag& ToSlot,
	int32 Count)
{
	// Belt-and-braces: only ever reached on the authority.
	if (!HasAuthorityToMutate())
	{
		return EInvUI_MoveResult::NoAuthority;
	}

	UInvUI_ContainerRegistry* Registry = GetRegistry();
	if (!Registry)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] Mediator move failed: no container registry on the authority."));
		return EInvUI_MoveResult::UnknownContainer;
	}

	// Re-resolve BOTH containers server-side from the stable ids. The client never supplied a
	// pointer; we look up the live read-only seam ourselves.
	TScriptInterface<IInvUI_ItemContainer> FromObj = Registry->ResolveContainer(FromContainer);
	TScriptInterface<IInvUI_ItemContainer> ToObj = Registry->ResolveContainer(ToContainer);

	if (FromObj.GetObject() == nullptr || ToObj.GetObject() == nullptr)
	{
		UE_LOG(LogDP, Verbose, TEXT("[InvUI] Mediator move rejected: container(s) did not resolve (%s -> %s)."),
			*FromContainer.ToString(), *ToContainer.ToString());
		return EInvUI_MoveResult::UnknownContainer;
	}

	AActor* Requestor = GetOwner();

	// Authority-only access gate on BOTH endpoints (e.g. proximity/ownership/lock).
	if (!PassesAccessGate(FromObj, Requestor) || !PassesAccessGate(ToObj, Requestor))
	{
		UE_LOG(LogDP, Verbose, TEXT("[InvUI] Mediator move denied by access gate: %s -> %s (requestor %s)."),
			*FromContainer.ToString(), *ToContainer.ToString(),
			*GetNameSafe(Requestor));
		return EInvUI_MoveResult::AccessDenied;
	}

	// Verify the SOURCE slot actually holds something movable right now.
	FInvUI_SlotState SourceState;
	const bool bHasSource = IInvUI_ItemContainer::Execute_GetSlot(FromObj.GetObject(), FromSlot, SourceState);
	if (!bHasSource || SourceState.IsEmpty())
	{
		UE_LOG(LogDP, Verbose, TEXT("[InvUI] Mediator move rejected: source slot %s::%s is empty."),
			*FromContainer.ToString(), *FromSlot.ToString());
		return EInvUI_MoveResult::SourceEmpty;
	}

	// Advisory destination check (the same predicate the UI greys out invalid targets with). Only
	// consulted when a concrete destination slot is named; an empty ToSlot defers placement to the
	// backend (first-free / merge), which the backend itself validates authoritatively.
	if (ToSlot.IsValid())
	{
		const bool bCanAccept = IInvUI_ItemContainer::Execute_QueryCanAccept(ToObj.GetObject(), SourceState, ToSlot);
		if (!bCanAccept)
		{
			UE_LOG(LogDP, Verbose, TEXT("[InvUI] Mediator move rejected by destination predicate: %s::%s -> %s::%s."),
				*FromContainer.ToString(), *FromSlot.ToString(),
				*ToContainer.ToString(), *ToSlot.ToString());
			return EInvUI_MoveResult::Rejected;
		}
	}

	// Authorized. Emit the authoritative move INTENT on the core message bus; the concrete backend
	// (owner of the replicated item state) applies the mutation under its own authority guard. This
	// module never mutates backend items directly — there is no generic mutate seam by design.
	UDP_MessageBusSubsystem* Bus =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_MessageBusSubsystem>(this);
	if (!Bus)
	{
		UE_LOG(LogDP, Warning, TEXT("[InvUI] Mediator authorized a move but the message bus is unavailable."));
		return EInvUI_MoveResult::UnknownContainer;
	}

	FInvUI_MoveIntentPayload Intent;
	Intent.FromContainer = FromContainer;
	Intent.FromSlot = FromSlot;
	Intent.ToContainer = ToContainer;
	Intent.ToSlot = ToSlot;
	// Clamp the requested count to what the source actually holds; 0 still means "whole stack".
	Intent.Count = (Count <= 0) ? 0 : FMath::Min(Count, SourceState.Count);

	const FInstancedStruct Payload = FInstancedStruct::Make(Intent);
	Bus->BroadcastPayload(InvUITags::Intent_Move, Payload, Requestor);

	UE_LOG(LogDP, Verbose, TEXT("[InvUI] Mediator dispatched move intent: %s::%s -> %s::%s x%d."),
		*FromContainer.ToString(), *FromSlot.ToString(),
		*ToContainer.ToString(), *ToSlot.ToString(), Intent.Count);
	return EInvUI_MoveResult::Success;
}

void UInvUI_ContainerMediatorComponent::Client_ReportMoveResult_Implementation(
	EInvUI_MoveResult Result,
	FInvUI_ContainerInstanceId FromContainer, FGameplayTag FromSlot,
	FInvUI_ContainerInstanceId ToContainer, FGameplayTag ToSlot,
	int32 Count)
{
	// On the owning client, surface the outcome for UI feedback (sound, error toast, re-sync).
	BroadcastResult(Result, FromContainer, FromSlot, ToContainer, ToSlot, Count);
}

void UInvUI_ContainerMediatorComponent::BroadcastResult(
	EInvUI_MoveResult Result,
	const FInvUI_ContainerInstanceId& FromContainer, const FGameplayTag& FromSlot,
	const FInvUI_ContainerInstanceId& ToContainer, const FGameplayTag& ToSlot,
	int32 Count)
{
	OnMoveResult.Broadcast(Result, FromContainer, FromSlot, ToContainer, ToSlot, Count);
}
