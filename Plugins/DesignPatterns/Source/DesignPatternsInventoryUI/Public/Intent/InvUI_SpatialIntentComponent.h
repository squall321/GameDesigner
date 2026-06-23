// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Seam/InvUI_ItemContainer.h"
#include "Mediator/InvUI_ContainerMediatorComponent.h" // for EInvUI_MoveResult (shared outcome enum)
#include "InvUI_SpatialIntentComponent.generated.h"

class UInvUI_ContainerRegistry;

/**
 * Authoritative intent: place a stack at an EXPLICIT cell in a spatial container, optionally rotated.
 *
 * Broadcast on the core message bus (InvUITags::Intent_Place) by the AUTHORITY once a spatial place
 * has been authorized, so the concrete backend applies the actual mutation. Parallel to the shipped
 * FInvUI_MoveIntentPayload — all stable identities/values, never a raw pointer, never replicated
 * directly (the bus wraps it in an FInstancedStruct).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_SpatialPlaceIntentPayload
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

	/** Destination top-left cell in the spatial grid. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	FIntPoint TargetCell = FIntPoint::ZeroValue;

	/** Whether the item should be rotated when placed. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	bool bRotated = false;

	/** Units to move; 0 = the whole source stack. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	int32 Count = 0;

	FInvUI_SpatialPlaceIntentPayload() = default;
};

/**
 * Authoritative intent: rotate the item in a slot in place. Broadcast on InvUITags::Intent_Rotate.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_RotateIntentPayload
{
	GENERATED_BODY()

	/** Container holding the item to rotate. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	FInvUI_ContainerInstanceId Container;

	/** Slot whose item rotates. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Intent")
	FGameplayTag SlotTag;

	FInvUI_RotateIntentPayload() = default;
};

/**
 * Player-owned ADDITIVE sibling to UInvUI_ContainerMediatorComponent for SPATIAL-only intents
 * (rotate-in-place, place-at-explicit-cell) the base move mediator does not cover.
 *
 * It RE-IMPLEMENTS (does not call) the mediator's validate-by-identity pattern using ONLY public
 * APIs: UInvUI_ContainerRegistry::Get(this)->ResolveContainer + IInvUI_ContainerAccess::CanAccess +
 * IInvUI_ItemContainer::QueryCanAccept. It does NOT touch any mediator member (those are
 * protected/private). On success it emits an FInstancedStruct-wrapped intent on the core message bus
 * (Intent_Place / Intent_Rotate) for the backend to apply under its own authority.
 *
 * REPLICATION: SetIsReplicatedByDefault(true) for existence only; it holds NO authoritative
 * replicated state. Every Server_* impl opens with an authority guard at the TOP; the _Validate
 * companions do structural-only checks (a client cannot name a raw target — only stable ids the
 * server re-resolves).
 */
UCLASS(ClassGroup = (InventoryUI), meta = (BlueprintSpawnableComponent, DisplayName = "InvUI Spatial Intent"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_SpatialIntentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInvUI_SpatialIntentComponent();

	/** Fired (server and requesting client) when a spatial intent resolves, for UI feedback. */
	UPROPERTY(BlueprintAssignable, Category = "InvUI|Spatial")
	FInvUI_OnMoveResult OnSpatialResult;

	/**
	 * Client-side entry: request rotating the item in (Container, SlotTag). Routes to the server on a
	 * client; validates+dispatches directly on the authority. The UI calls ONLY this.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Spatial")
	void RequestRotate(FInvUI_ContainerInstanceId Container, FGameplayTag SlotTag);

	/**
	 * Client-side entry: request placing FromSlot's stack at an explicit Cell in ToContainer,
	 * optionally rotated, moving Count units (0 = whole stack).
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Spatial")
	void RequestPlaceAtCell(FInvUI_ContainerInstanceId From, FGameplayTag FromSlot,
		FInvUI_ContainerInstanceId To, FIntPoint Cell, bool bRotated, int32 Count);

	/** Server RPC: validate and dispatch a rotate. Re-resolves the container; never trusts pointers. */
	UFUNCTION(Server, Reliable, WithValidation, Category = "InvUI|Spatial")
	void Server_Rotate(FInvUI_ContainerInstanceId Container, FGameplayTag SlotTag);

	/** Server RPC: validate and dispatch a place-at-cell. */
	UFUNCTION(Server, Reliable, WithValidation, Category = "InvUI|Spatial")
	void Server_PlaceAtCell(FInvUI_ContainerInstanceId From, FGameplayTag FromSlot,
		FInvUI_ContainerInstanceId To, FIntPoint Cell, bool bRotated, int32 Count);

protected:
	/** True when this component's owner has authority over container mutations. */
	bool HasAuthorityToMutate() const;

	/** Resolve the world container registry for this component's world (may be null). */
	UInvUI_ContainerRegistry* GetRegistry() const;

	/**
	 * Run the optional access gate on a resolved container against the requestor. True when the
	 * container does not implement IInvUI_ContainerAccess or grants access. Mirrors the base mediator.
	 */
	bool PassesAccessGate(const TScriptInterface<IInvUI_ItemContainer>& Container, AActor* Requestor) const;

	/**
	 * Authoritative core for rotate: re-resolve the container, run the access gate, then broadcast
	 * the Intent_Rotate payload. Returns the outcome. Called only on the authority.
	 */
	EInvUI_MoveResult ValidateAndDispatchRotate(const FInvUI_ContainerInstanceId& Container, const FGameplayTag& SlotTag);

	/**
	 * Authoritative core for place-at-cell: re-resolve both containers, run the access gates and the
	 * advisory destination predicate, then broadcast the Intent_Place payload. Called on the authority.
	 */
	EInvUI_MoveResult ValidateAndDispatchPlace(const FInvUI_ContainerInstanceId& From, const FGameplayTag& FromSlot,
		const FInvUI_ContainerInstanceId& To, const FIntPoint& Cell, bool bRotated, int32 Count);

	/** Mirror a resolved spatial result back to the requesting client. */
	UFUNCTION(Client, Reliable)
	void Client_ReportSpatialResult(EInvUI_MoveResult Result,
		FInvUI_ContainerInstanceId FromContainer, FGameplayTag FromSlot,
		FInvUI_ContainerInstanceId ToContainer, FGameplayTag ToSlot, int32 Count);

	/** Broadcast OnSpatialResult locally (server and client paths). */
	void BroadcastResult(EInvUI_MoveResult Result,
		const FInvUI_ContainerInstanceId& FromContainer, const FGameplayTag& FromSlot,
		const FInvUI_ContainerInstanceId& ToContainer, const FGameplayTag& ToSlot, int32 Count);

private:
	/** The owning actor re-derived as the access-gate requestor (never a client-sent pointer). */
	AActor* GetRequestor() const;
};
