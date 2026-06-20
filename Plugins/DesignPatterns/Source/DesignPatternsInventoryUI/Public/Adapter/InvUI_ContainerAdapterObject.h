// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/ScriptInterface.h"
#include "Seam/InvUI_ItemContainer.h"
#include "InvUI_ContainerAdapterObject.generated.h"

/**
 * Base adapter object that bridges a concrete backend's *dynamic* change delegate to a single
 * native FSimpleMulticastDelegate the viewmodel layer subscribes to.
 *
 * WHY: every backend signals "my contents changed" with its own bespoke dynamic multicast
 * delegate (e.g. the RPG inventory's FRPG_OnInventoryChanged). The viewmodel doesn't want to
 * know about any of those types. A concrete adapter (living in the genre module that owns the
 * backend) binds that backend delegate in its override of BindBackend() and, when it fires,
 * calls NotifyContainerChanged() on this base, which re-broadcasts the type-erased
 * OnContainerChanged. The viewmodel binds only to OnContainerChanged.
 *
 * This base also (optionally) implements IInvUI_ItemContainer by forwarding to a target the
 * concrete adapter supplies, so an adapter can either BE the container seam or wrap one. Most
 * concrete adapters set TargetContainer to the backend (when the backend itself implements the
 * seam) and leave the forwarding implementation in place; an adapter for a backend that does
 * NOT implement the seam overrides the IInvUI_ItemContainer methods directly.
 *
 * The shipped RPG adapter (URPG_InvUIContainerAdapter) lives in the DesignPatternsRPG module
 * and is documented there; this module ships only the reusable base so it never hard-depends
 * on any genre backend.
 *
 * GC: TargetContainer is held as a TScriptInterface (object kept alive by its real owner — the
 * adapter does not own the backend). Backend is a TWeakObjectPtr used only to unbind safely.
 */
UCLASS(Blueprintable, BlueprintType, meta = (DisplayName = "InvUI Container Adapter"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_ContainerAdapterObject : public UObject, public IInvUI_ItemContainer
{
	GENERATED_BODY()

public:
	/**
	 * Type-erased "contents changed" delegate the viewmodel binds to. Native (not dynamic) so it
	 * is cheap and does not require UFUNCTION targets. Fired from NotifyContainerChanged().
	 */
	FSimpleMulticastDelegate OnContainerChanged;

	/**
	 * Wire this adapter to a backend object. The base records BoundBackend (weak, for safe
	 * unbinding) and calls the virtual BindBackend hook where a concrete adapter binds the
	 * backend's dynamic delegate to HandleBackendChanged. Calling Initialize again first tears
	 * down any previous binding. The backend object should also implement (or be wrapped to
	 * implement) IInvUI_ItemContainer; pass it as Container so the forwarding seam works.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Adapter")
	void InitializeAdapter(UObject* InBackend, const TScriptInterface<IInvUI_ItemContainer>& Container);

	/** Tear down the backend binding (idempotent). Called from InitializeAdapter and on destroy. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Adapter")
	void ShutdownAdapter();

	/** The container this adapter forwards the IInvUI_ItemContainer seam to (may be empty). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Adapter")
	TScriptInterface<IInvUI_ItemContainer> GetTargetContainer() const { return TargetContainer; }

	/** True once a backend has been bound and not yet shut down. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Adapter")
	bool IsBound() const { return BoundBackend.IsValid(); }

	//~ Begin UObject
	virtual void BeginDestroy() override;
	//~ End UObject

	//~ Begin IInvUI_ItemContainer (default: forward to TargetContainer)
	virtual FInvUI_ContainerInstanceId GetContainerInstanceId_Implementation() const override;
	virtual void GetSlots_Implementation(TArray<FInvUI_SlotState>& OutSlots) const override;
	virtual bool GetSlot_Implementation(FGameplayTag SlotTag, FInvUI_SlotState& OutSlot) const override;
	virtual int32 GetCapacity_Implementation() const override;
	virtual bool QueryCanAccept_Implementation(const FInvUI_SlotState& Candidate, FGameplayTag SlotTag) const override;
	virtual FInvUI_OnContainerChangedDynamic& GetOnContainerChangedDelegate() override;
	//~ End IInvUI_ItemContainer

protected:
	/**
	 * Concrete-adapter hook: bind the backend's dynamic change delegate so it routes to this
	 * adapter (typically by AddDynamic-ing HandleBackendChanged, or a thin local UFUNCTION that
	 * calls NotifyContainerChanged). The base implementation does nothing — a backend whose seam
	 * exposes GetOnContainerChangedDelegate is bound automatically by the base instead.
	 *
	 * @param InBackend The backend object passed to InitializeAdapter (never null here).
	 */
	virtual void BindBackend(UObject* InBackend);

	/** Concrete-adapter hook: undo whatever BindBackend wired up. Base unbinds the seam delegate. */
	virtual void UnbindBackend(UObject* InBackend);

	/**
	 * Call from a concrete adapter (or the auto-bound seam delegate) when the backend's contents
	 * change. Re-broadcasts the type-erased OnContainerChanged. Safe to call any time after
	 * InitializeAdapter.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Adapter")
	void NotifyContainerChanged();

	/**
	 * Dynamic glue target for backends that expose a seam dynamic delegate
	 * (IInvUI_ItemContainer::GetOnContainerChangedDelegate). The base AddDynamic-binds this in
	 * BindBackend and it simply forwards to NotifyContainerChanged.
	 */
	UFUNCTION()
	void HandleSeamContainerChanged(const FInvUI_ContainerInstanceId& ContainerId);

	/** The container the seam forwards to. Kept alive by its real owner; we only reference it. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Adapter")
	TScriptInterface<IInvUI_ItemContainer> TargetContainer;

	/** Non-owning weak ref to the bound backend, for safe unbinding even after GC. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> BoundBackend;
};
