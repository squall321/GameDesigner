// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Intent/InvUI_SpatialIntentComponent.h"
#include "Registry/InvUI_ContainerRegistry.h"
#include "Seam/InvUI_ContainerAccess.h"
#include "InvUI_NativeTags.h"
#include "MessageBus/DPMessageBusSubsystem.h"
#include "Core/DPLog.h"

UInvUI_SpatialIntentComponent::UInvUI_SpatialIntentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	// Replicate for existence/owner only; this component holds NO authoritative replicated state.
	SetIsReplicatedByDefault(true);
}

bool UInvUI_SpatialIntentComponent::HasAuthorityToMutate() const
{
	const AActor* Owner = GetOwner();
	return Owner != nullptr && Owner->HasAuthority();
}

UInvUI_ContainerRegistry* UInvUI_SpatialIntentComponent::GetRegistry() const
{
	return UInvUI_ContainerRegistry::Get(this);
}

AActor* UInvUI_SpatialIntentComponent::GetRequestor() const
{
	// The owning actor (pawn/controller) re-derived on the server — never a client-sent pointer.
	return GetOwner();
}

bool UInvUI_SpatialIntentComponent::PassesAccessGate(
	const TScriptInterface<IInvUI_ItemContainer>& Container, AActor* Requestor) const
{
	UObject* Obj = Container.GetObject();
	if (Obj == nullptr)
	{
		return false;
	}
	// A container that does not implement the access gate is unconditionally accessible (the
	// authority still re-derived the requestor and re-resolved the target).
	if (!Obj->GetClass()->ImplementsInterface(UInvUI_ContainerAccess::StaticClass()))
	{
		return true;
	}
	return IInvUI_ContainerAccess::Execute_CanAccess(Obj, Requestor);
}

// ---- Client entry points ----

void UInvUI_SpatialIntentComponent::RequestRotate(FInvUI_ContainerInstanceId Container, FGameplayTag SlotTag)
{
	if (HasAuthorityToMutate())
	{
		const EInvUI_MoveResult Result = ValidateAndDispatchRotate(Container, SlotTag);
		BroadcastResult(Result, Container, SlotTag, Container, SlotTag, 0);
	}
	else
	{
		Server_Rotate(Container, SlotTag);
	}
}

void UInvUI_SpatialIntentComponent::RequestPlaceAtCell(FInvUI_ContainerInstanceId From, FGameplayTag FromSlot,
	FInvUI_ContainerInstanceId To, FIntPoint Cell, bool bRotated, int32 Count)
{
	if (HasAuthorityToMutate())
	{
		const EInvUI_MoveResult Result = ValidateAndDispatchPlace(From, FromSlot, To, Cell, bRotated, Count);
		BroadcastResult(Result, From, FromSlot, To, FGameplayTag(), Count);
	}
	else
	{
		Server_PlaceAtCell(From, FromSlot, To, Cell, bRotated, Count);
	}
}

// ---- Server RPCs ----

bool UInvUI_SpatialIntentComponent::Server_Rotate_Validate(FInvUI_ContainerInstanceId Container, FGameplayTag /*SlotTag*/)
{
	// Structural-only check: the id must be well-formed (the server re-resolves it authoritatively).
	return Container.IsValid();
}

void UInvUI_SpatialIntentComponent::Server_Rotate_Implementation(FInvUI_ContainerInstanceId Container, FGameplayTag SlotTag)
{
	if (!HasAuthorityToMutate())
	{
		return; // authority guard at the TOP
	}
	const EInvUI_MoveResult Result = ValidateAndDispatchRotate(Container, SlotTag);
	Client_ReportSpatialResult(Result, Container, SlotTag, Container, SlotTag, 0);
	BroadcastResult(Result, Container, SlotTag, Container, SlotTag, 0);
}

bool UInvUI_SpatialIntentComponent::Server_PlaceAtCell_Validate(FInvUI_ContainerInstanceId From, FGameplayTag /*FromSlot*/,
	FInvUI_ContainerInstanceId To, FIntPoint /*Cell*/, bool /*bRotated*/, int32 Count)
{
	return From.IsValid() && To.IsValid() && Count >= 0;
}

void UInvUI_SpatialIntentComponent::Server_PlaceAtCell_Implementation(FInvUI_ContainerInstanceId From, FGameplayTag FromSlot,
	FInvUI_ContainerInstanceId To, FIntPoint Cell, bool bRotated, int32 Count)
{
	if (!HasAuthorityToMutate())
	{
		return; // authority guard at the TOP
	}
	const EInvUI_MoveResult Result = ValidateAndDispatchPlace(From, FromSlot, To, Cell, bRotated, Count);
	Client_ReportSpatialResult(Result, From, FromSlot, To, FGameplayTag(), Count);
	BroadcastResult(Result, From, FromSlot, To, FGameplayTag(), Count);
}

// ---- Authoritative validation cores ----

EInvUI_MoveResult UInvUI_SpatialIntentComponent::ValidateAndDispatchRotate(
	const FInvUI_ContainerInstanceId& Container, const FGameplayTag& SlotTag)
{
	UInvUI_ContainerRegistry* Registry = GetRegistry();
	if (Registry == nullptr)
	{
		return EInvUI_MoveResult::UnknownContainer;
	}
	TScriptInterface<IInvUI_ItemContainer> Resolved = Registry->ResolveContainer(Container);
	if (Resolved.GetObject() == nullptr)
	{
		return EInvUI_MoveResult::UnknownContainer;
	}

	AActor* Requestor = GetRequestor();
	if (!PassesAccessGate(Resolved, Requestor))
	{
		return EInvUI_MoveResult::AccessDenied;
	}

	// Source slot must hold something to rotate.
	FInvUI_SlotState State;
	if (!IInvUI_ItemContainer::Execute_GetSlot(Resolved.GetObject(), SlotTag, State) || !State.IsOccupied())
	{
		return EInvUI_MoveResult::SourceEmpty;
	}

	// Emit the authoritative rotate intent for the backend to apply.
	UDP_MessageBusSubsystem* Bus = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			Bus = GI->GetSubsystem<UDP_MessageBusSubsystem>();
		}
	}
	if (Bus == nullptr)
	{
		return EInvUI_MoveResult::NoAuthority;
	}

	FInvUI_RotateIntentPayload Payload;
	Payload.Container = Container;
	Payload.SlotTag = SlotTag;

	FInstancedStruct Wrapped = FInstancedStruct::Make(Payload);
	Bus->BroadcastPayload(InvUITags::Intent_Rotate, Wrapped, GetOwner());
	return EInvUI_MoveResult::Success;
}

EInvUI_MoveResult UInvUI_SpatialIntentComponent::ValidateAndDispatchPlace(
	const FInvUI_ContainerInstanceId& From, const FGameplayTag& FromSlot,
	const FInvUI_ContainerInstanceId& To, const FIntPoint& Cell, bool bRotated, int32 Count)
{
	UInvUI_ContainerRegistry* Registry = GetRegistry();
	if (Registry == nullptr)
	{
		return EInvUI_MoveResult::UnknownContainer;
	}

	TScriptInterface<IInvUI_ItemContainer> FromResolved = Registry->ResolveContainer(From);
	TScriptInterface<IInvUI_ItemContainer> ToResolved = Registry->ResolveContainer(To);
	if (FromResolved.GetObject() == nullptr || ToResolved.GetObject() == nullptr)
	{
		return EInvUI_MoveResult::UnknownContainer;
	}

	AActor* Requestor = GetRequestor();
	if (!PassesAccessGate(FromResolved, Requestor) || !PassesAccessGate(ToResolved, Requestor))
	{
		return EInvUI_MoveResult::AccessDenied;
	}

	// Source slot must hold something to place.
	FInvUI_SlotState SourceState;
	if (!IInvUI_ItemContainer::Execute_GetSlot(FromResolved.GetObject(), FromSlot, SourceState) || !SourceState.IsOccupied())
	{
		return EInvUI_MoveResult::SourceEmpty;
	}

	// Advisory destination check (the spatial backend re-validates the exact cell authoritatively;
	// QueryCanAccept here is the type/restriction gate, slot tag left empty since this is cell-based).
	if (!IInvUI_ItemContainer::Execute_QueryCanAccept(ToResolved.GetObject(), SourceState, FGameplayTag()))
	{
		return EInvUI_MoveResult::Rejected;
	}

	UDP_MessageBusSubsystem* Bus = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (UGameInstance* GI = World->GetGameInstance())
		{
			Bus = GI->GetSubsystem<UDP_MessageBusSubsystem>();
		}
	}
	if (Bus == nullptr)
	{
		return EInvUI_MoveResult::NoAuthority;
	}

	FInvUI_SpatialPlaceIntentPayload Payload;
	Payload.FromContainer = From;
	Payload.FromSlot = FromSlot;
	Payload.ToContainer = To;
	Payload.TargetCell = Cell;
	Payload.bRotated = bRotated;
	Payload.Count = FMath::Max(0, Count);

	FInstancedStruct Wrapped = FInstancedStruct::Make(Payload);
	Bus->BroadcastPayload(InvUITags::Intent_Place, Wrapped, GetOwner());
	return EInvUI_MoveResult::Success;
}

void UInvUI_SpatialIntentComponent::Client_ReportSpatialResult_Implementation(EInvUI_MoveResult Result,
	FInvUI_ContainerInstanceId FromContainer, FGameplayTag FromSlot,
	FInvUI_ContainerInstanceId ToContainer, FGameplayTag ToSlot, int32 Count)
{
	BroadcastResult(Result, FromContainer, FromSlot, ToContainer, ToSlot, Count);
}

void UInvUI_SpatialIntentComponent::BroadcastResult(EInvUI_MoveResult Result,
	const FInvUI_ContainerInstanceId& FromContainer, const FGameplayTag& FromSlot,
	const FInvUI_ContainerInstanceId& ToContainer, const FGameplayTag& ToSlot, int32 Count)
{
	OnSpatialResult.Broadcast(Result, FromContainer, FromSlot, ToContainer, ToSlot, Count);
}
