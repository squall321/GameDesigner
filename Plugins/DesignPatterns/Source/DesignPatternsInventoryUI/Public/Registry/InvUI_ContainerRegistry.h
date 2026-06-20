// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UObject/ScriptInterface.h"
#include "UObject/WeakInterfacePtr.h"
#include "Seam/InvUI_ItemContainer.h"
#include "InvUI_ContainerRegistry.generated.h"

/** Fired when a container is registered or unregistered, so open windows can refresh/close. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FInvUI_OnRegistryChanged,
	const FInvUI_ContainerInstanceId&, ContainerId, bool, bRegistered);

/**
 * World-scoped registry mapping a stable FInvUI_ContainerInstanceId to the live container that
 * implements IInvUI_ItemContainer.
 *
 * This is the indirection that lets the window UI (and the server-side intent router) find a
 * container by IDENTITY rather than by pointer: backends register themselves on spawn/begin-play
 * and unregister on end-play; windows and the router resolve the id to the live interface. The
 * stored reference is a TWeakInterfacePtr so the registry never keeps a backend (often an
 * actor component) alive past its natural lifetime; stale entries are pruned on resolve and on
 * a periodic compaction.
 *
 * It is a plain world subsystem holding NO replicated state — it is a per-machine lookup table.
 * The authoritative container state lives on the backend component/actor; this registry only
 * indexes who currently exists. It publishes itself into the service locator under
 * InvUITags::Service_ContainerRegistry so non-world-context code can find it.
 */
UCLASS()
class DESIGNPATTERNSINVENTORYUI_API UInvUI_ContainerRegistry : public UDP_WorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority helper; declare our own. The registry itself is
	 * not authority-gated (both client and server index their local containers), but the
	 * server-side intent router uses this to decide whether it may apply a mutation.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	/**
	 * Register Container under its own GetContainerInstanceId(). Rejects (and logs) a null
	 * container, a container with an invalid id, or a *different* live container already bound
	 * to the same id. Re-registering the same object, or replacing a stale (GC'd) binding, is
	 * allowed. Returns true on success and fires OnRegistryChanged(id, true).
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Registry")
	bool RegisterContainer(const TScriptInterface<IInvUI_ItemContainer>& Container);

	/**
	 * Remove the binding for ContainerId (if present) and fire OnRegistryChanged(id, false).
	 * Returns true if a binding existed.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Registry")
	bool UnregisterContainer(const FInvUI_ContainerInstanceId& ContainerId);

	/**
	 * Convenience: unregister whatever id Container currently reports. Safe to call from a
	 * backend's EndPlay even if it was never registered.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Registry")
	bool UnregisterContainerObject(const TScriptInterface<IInvUI_ItemContainer>& Container);

	/**
	 * Resolve ContainerId to the live container, or an empty interface if unbound / stale.
	 * Prunes the slot if the bound object has been GC'd.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Registry")
	TScriptInterface<IInvUI_ItemContainer> ResolveContainer(const FInvUI_ContainerInstanceId& ContainerId) const;

	/** True if ContainerId is currently bound to a live container. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Registry")
	bool IsRegistered(const FInvUI_ContainerInstanceId& ContainerId) const;

	/** Every currently-bound (live) container id. Prunes stale slots as a side effect. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Registry")
	TArray<FInvUI_ContainerInstanceId> GetRegisteredIds() const;

	/** Number of currently-bound (live or not-yet-pruned) slots. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Registry")
	int32 GetContainerCount() const;

	/** Fires on every register/unregister so open windows can react. */
	UPROPERTY(BlueprintAssignable, Category = "InvUI|Registry")
	FInvUI_OnRegistryChanged OnRegistryChanged;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/** Static helper that resolves the registry for a world-context object (may be null). */
	static UInvUI_ContainerRegistry* Get(const UObject* WorldContextObject);

private:
	/** One indexed entry: a weak (non-owning) reference to the registered container. */
	struct FEntry
	{
		TWeakInterfacePtr<IInvUI_ItemContainer> Container;
	};

	/**
	 * Identity -> entry. The registry never owns its containers (weak refs only). Mutable so the
	 * const resolve/list/dump paths can prune slots whose backend has been GC'd.
	 */
	mutable TMap<FInvUI_ContainerInstanceId, FEntry> Entries;

	/** Drop any entries whose container object has been GC'd. Const: mutates only the cache. */
	void PruneStale() const;

	/** Publish/withdraw ourselves in the service locator (no-op if the locator is absent). */
	void PublishToServiceLocator(bool bRegister);
};
