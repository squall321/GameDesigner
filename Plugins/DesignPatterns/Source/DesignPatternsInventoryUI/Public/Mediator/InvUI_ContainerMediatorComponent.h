// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"

#include "Seam/InvUI_ItemContainer.h"

#include "InvUI_ContainerMediatorComponent.generated.h"

class IInvUI_ItemContainer;
class UInvUI_ContainerRegistry;

/** Outcome of a requested identity move, surfaced to the requesting client for UI feedback. */
UENUM(BlueprintType)
enum class EInvUI_MoveResult : uint8
{
	/** The move was authorized and dispatched to the backend on the authority. */
	Success,
	/** One or both container ids failed to resolve to a live container on the authority. */
	UnknownContainer,
	/** The access gate (IInvUI_ContainerAccess::CanAccess) rejected the requestor. */
	AccessDenied,
	/** The advisory destination check (QueryCanAccept) rejected the move. */
	Rejected,
	/** The source slot held nothing movable (emptied between request and execution). */
	SourceEmpty,
	/** The component lacked authority and the request could not be honored locally. */
	NoAuthority
};

/**
 * Payload broadcast on the core message bus (channel InvUITags::Intent_Move) by the AUTHORITY
 * once a UI move has been authorized, so the concrete backend applies the actual mutation.
 *
 * This module never mutates a backend's items directly (no generic mutate seam exists by design);
 * it validates the move by identity and emits this authoritative intent. The backend (which owns
 * the replicated item state) listens on the channel and performs the move under its own authority
 * guard. All fields are stable identities/values — nothing here is a raw pointer.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_MoveIntentPayload
{
	GENERATED_BODY()

	/** Source container id. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	FInvUI_ContainerInstanceId FromContainer;

	/** Source slot tag. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	FGameplayTag FromSlot;

	/** Destination container id. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	FInvUI_ContainerInstanceId ToContainer;

	/** Destination slot tag (empty = first free / merge target chosen by the backend). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	FGameplayTag ToSlot;

	/** Units to move; 0 means "the whole source stack" (the backend re-derives/clamps). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	int32 Count = 0;

	FInvUI_MoveIntentPayload() = default;
};

/**
 * Broadcast feedback for a completed move attempt.
 *
 * @param Result        Outcome classification.
 * @param FromContainer Source container id (echoed for correlation).
 * @param FromSlot      Source slot tag.
 * @param ToContainer   Destination container id.
 * @param ToSlot        Destination slot tag (may be empty for "first free / merge").
 * @param Count         Units requested (echoed; the backend reports the final applied count).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_SixParams(FInvUI_OnMoveResult,
	EInvUI_MoveResult, Result,
	FInvUI_ContainerInstanceId, FromContainer,
	FGameplayTag, FromSlot,
	FInvUI_ContainerInstanceId, ToContainer,
	FGameplayTag, ToSlot,
	int32, Count);

/**
 * Per-player MEDIATOR between the inventory UI and the authoritative container backends.
 *
 * Lives on a PLAYER-OWNED actor (player controller or pawn). The UI never moves items by pointer
 * or index: a slot/grid asks this mediator to move BY IDENTITY (source container id + slot tag ->
 * destination container id + slot tag + count). The mediator forwards to the server, which
 * independently RE-RESOLVES both containers from the registry, checks the IInvUI_ContainerAccess
 * gate against the re-derived requestor, validates the move by identity, then emits an
 * authoritative move intent on the core message bus for the concrete backend to apply. The client
 * is never trusted: it cannot name a raw target, only stable ids the server re-resolves.
 *
 * REPLICATION: this component replicates only so its existence/owner is known; it holds NO
 * authoritative game state (that lives on the container backends). The single mutating entry point
 * is an authority-guarded server RPC; the result is mirrored back to the requesting client.
 */
UCLASS(ClassGroup = (InventoryUI), meta = (BlueprintSpawnableComponent, DisplayName = "InvUI Container Mediator"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_ContainerMediatorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInvUI_ContainerMediatorComponent();

	/** Fired (on the requesting client and the server) when a move attempt resolves. */
	UPROPERTY(BlueprintAssignable, Category = "InvUI|Mediator")
	FInvUI_OnMoveResult OnMoveResult;

	/**
	 * Client-side entry point. Requests a move by identity. On a client this routes to the server
	 * RPC; on the authority it validates+dispatches directly. The UI calls ONLY this.
	 *
	 * @param FromContainer Source container id.
	 * @param FromSlot      Source slot tag.
	 * @param ToContainer   Destination container id.
	 * @param ToSlot        Destination slot tag (empty = first free / merge target chosen by backend).
	 * @param Count         Units to move; 0 means "the whole source stack" (backend re-derives).
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Mediator")
	void RequestMoveByIdentity(
		FInvUI_ContainerInstanceId FromContainer, FGameplayTag FromSlot,
		FInvUI_ContainerInstanceId ToContainer, FGameplayTag ToSlot,
		int32 Count);

	/**
	 * Server RPC: validate and dispatch the move by identity. The server re-resolves both
	 * containers from the registry, checks IInvUI_ContainerAccess and the advisory destination
	 * predicate, then emits the authoritative move intent. Never trusts client-named pointers.
	 */
	UFUNCTION(Server, Reliable, WithValidation, Category = "InvUI|Mediator")
	void Server_MoveByIdentity(
		FInvUI_ContainerInstanceId FromContainer, FGameplayTag FromSlot,
		FInvUI_ContainerInstanceId ToContainer, FGameplayTag ToSlot,
		int32 Count);

protected:
	/** True when this component's owner has authority over container mutations. */
	bool HasAuthorityToMutate() const;

	/** Resolve the world container registry for this component's world. */
	UInvUI_ContainerRegistry* GetRegistry() const;

	/**
	 * Authoritative core: re-resolve both containers, run the access + destination checks, and on
	 * success broadcast the authoritative move intent on the message bus. Returns the outcome.
	 * Called only on the authority.
	 */
	EInvUI_MoveResult ValidateAndDispatchMove(
		const FInvUI_ContainerInstanceId& FromContainer, const FGameplayTag& FromSlot,
		const FInvUI_ContainerInstanceId& ToContainer, const FGameplayTag& ToSlot,
		int32 Count);

	/** Mirror a resolved result back to the requesting client for UI feedback (authority -> owner). */
	UFUNCTION(Client, Reliable)
	void Client_ReportMoveResult(
		EInvUI_MoveResult Result,
		FInvUI_ContainerInstanceId FromContainer, FGameplayTag FromSlot,
		FInvUI_ContainerInstanceId ToContainer, FGameplayTag ToSlot,
		int32 Count);

	/** Broadcast OnMoveResult locally (used on both server and client paths). */
	void BroadcastResult(
		EInvUI_MoveResult Result,
		const FInvUI_ContainerInstanceId& FromContainer, const FGameplayTag& FromSlot,
		const FInvUI_ContainerInstanceId& ToContainer, const FGameplayTag& ToSlot,
		int32 Count);

private:
	/**
	 * Run the optional access gate on a resolved container against the requestor. Returns true when
	 * the container either does not implement IInvUI_ContainerAccess or grants access.
	 */
	bool PassesAccessGate(const TScriptInterface<IInvUI_ItemContainer>& Container, AActor* Requestor) const;
};
