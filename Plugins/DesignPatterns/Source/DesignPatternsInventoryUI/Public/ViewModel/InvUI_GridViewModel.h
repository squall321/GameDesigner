// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Engine/StreamableManager.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Seam/InvUI_ItemContainer.h"
#include "Seam/InvUI_ItemDisplay.h"
#include "Strategy/InvUI_SortStrategy.h"
#include "Strategy/InvUI_LayoutStrategy.h"
#include "InvUI_GridViewModel.generated.h"

class UInvUI_SlotViewModel;
class UInvUI_ContainerAdapterObject;

/**
 * The window-level viewmodel that binds one IInvUI_ItemContainer (directly or through an
 * adapter) and exposes it to a grid/list/paper-doll view as a collection of per-slot viewmodels
 * plus a coarse StructureRevision counter for efficient binding.
 *
 * Binding model:
 *  - BindContainer wires this vm to a container and (preferred) an adapter's native
 *    OnContainerChanged, falling back to the seam's dynamic delegate. Any change triggers a
 *    Rebuild.
 *  - Rebuild reads the container's slots, applies the optional ItemFilter, the active
 *    SortStrategy, and the active LayoutStrategy, then diff-updates a pooled set of
 *    UInvUI_SlotViewModel objects (reusing instances so widgets aren't recreated). Only slots
 *    whose state actually changed re-broadcast their fields.
 *  - StructureRevision bumps once per Rebuild that changed the slot SET or order, so a view can
 *    cheaply rebind its item list on coarse changes while individual slot fields drive
 *    fine-grained updates.
 *  - Icons resolve asynchronously: the vm asks the IInvUI_ItemDisplay resolver for cached info
 *    first, then streams the soft icon via a shared FStreamableManager and pushes the loaded
 *    texture into the owning slot vm when ready. Requests are keyed so a slot reused for a
 *    different item cancels its stale load.
 *
 * Holds NO gameplay pointers beyond the seam (TScriptInterface). All owned UObjects are
 * UPROPERTY/TObjectPtr; the bound adapter is referenced weakly for delegate unbinding.
 */
UCLASS(BlueprintType, meta = (DisplayName = "InvUI Grid ViewModel"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_GridViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	UInvUI_GridViewModel();

	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		ContainerId = 0,
		StructureRevision,
		ColumnCount,
		RowCount,
		SlotCount,
		bBound,
		Num
	};

	//~ Begin UObject
	virtual void BeginDestroy() override;
	//~ End UObject

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/**
	 * Bind to Container, optionally via Adapter (whose native OnContainerChanged is preferred over
	 * the seam dynamic delegate). DisplayResolver supplies async icon/name resolution (may be
	 * empty — slots then show tag-derived names and no icon). Triggers an immediate Rebuild.
	 * Re-binding first unbinds the previous container.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void BindContainer(const TScriptInterface<IInvUI_ItemContainer>& Container,
		UInvUI_ContainerAdapterObject* Adapter,
		const TScriptInterface<IInvUI_ItemDisplay>& DisplayResolver);

	/** Unbind the current container and clear all slot viewmodels. Safe if not bound. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void UnbindContainer();

	/** Force a rebuild from the bound container's current state (normally automatic). */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void Rebuild();

	/** Set the active sort strategy and rebuild. Null clears sorting (container order kept). */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void SetSortStrategy(UInvUI_SortStrategy* InStrategy);

	/** Set the active layout strategy and rebuild. Null falls back to a single-row layout. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void SetLayoutStrategy(UInvUI_LayoutStrategy* InStrategy);

	/**
	 * Restrict displayed slots to items whose ItemTag matches InFilter (hierarchy-aware). An empty
	 * filter shows everything. Rebuilds. Empty slots are kept only when the filter is empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void SetItemFilter(const FGameplayTagContainer& InFilter);

	/** When true, empty slots are included in the display (fixed-slot containers); else hidden. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Grid")
	void SetShowEmptySlots(bool bInShow);

	/** The viewmodels for the currently displayed slots, in layout order (copied for BP safety). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Grid")
	TArray<UInvUI_SlotViewModel*> GetSlotViewModels() const;

	/** Find the slot viewmodel for a given slot identity, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "InvUI|Grid")
	UInvUI_SlotViewModel* FindSlotViewModel(FGameplayTag SlotTag) const;

	// --- Observable getters ---

	/** Identity of the bound container (invalid when unbound). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Grid") FInvUI_ContainerInstanceId GetContainerId() const { return ContainerId; }
	/** Bumps whenever the slot set/order changes; cheap coarse-change signal for the view. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Grid") int32 GetStructureRevision() const { return StructureRevision; }
	/** Layout column count (cell units). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Grid") int32 GetColumnCount() const { return ColumnCount; }
	/** Layout row count (cell units). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Grid") int32 GetRowCount() const { return RowCount; }
	/** Number of displayed slots. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Grid") int32 GetSlotCount() const { return ActiveSlotVMs.Num(); }
	/** True while a container is bound. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Grid") bool IsBound() const { return bBound; }

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Native handler bound to the adapter's OnContainerChanged. */
	void HandleAdapterChanged();

	/** Dynamic handler bound to the seam's GetOnContainerChangedDelegate (no-adapter path). */
	UFUNCTION()
	void HandleSeamChanged(const FInvUI_ContainerInstanceId& InContainerId);

	/** Acquire a slot viewmodel from the pool (or create one), used during Rebuild. */
	UInvUI_SlotViewModel* AcquireSlotVM();

	/** Kick async display resolution for SlotVM's current item via the resolver. */
	void RequestDisplayForSlot(UInvUI_SlotViewModel* SlotVM, const FGameplayTag& ItemTag);

	/** Begin streaming SoftIcon and push it to the slot vm if it still shows ItemTag when loaded. */
	void StreamIconForSlot(UInvUI_SlotViewModel* SlotVM, const FGameplayTag& ItemTag,
		const TSoftObjectPtr<UTexture2D>& SoftIcon, const FText& Name, const FText& Desc, const FLinearColor& Quality);

	/** Cancel any in-flight icon stream owned by SlotVM (slot reused for a different item). */
	void CancelStreamForSlot(const UInvUI_SlotViewModel* SlotVM);

	/** The bound container seam (object kept alive by its real owner). */
	UPROPERTY(Transient)
	TScriptInterface<IInvUI_ItemContainer> BoundContainer;

	/** The async display resolver seam (may be empty). */
	UPROPERTY(Transient)
	TScriptInterface<IInvUI_ItemDisplay> DisplayResolver;

	/** Weak ref to the bound adapter, for native-delegate unbinding. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UInvUI_ContainerAdapterObject> BoundAdapter;

	/** Active sort strategy (instanced, designer-set). Null = no reorder. */
	UPROPERTY(EditAnywhere, Instanced, Category = "InvUI|Grid")
	TObjectPtr<UInvUI_SortStrategy> SortStrategy;

	/** Active layout strategy (instanced, designer-set). Null = single-row fallback. */
	UPROPERTY(EditAnywhere, Instanced, Category = "InvUI|Grid")
	TObjectPtr<UInvUI_LayoutStrategy> LayoutStrategy;

	/** Optional item-tag filter; empty shows everything. */
	UPROPERTY(EditAnywhere, Category = "InvUI|Grid")
	FGameplayTagContainer ItemFilter;

	/** Whether to display empty slots (default true for fixed-slot containers). */
	UPROPERTY(EditAnywhere, Category = "InvUI|Grid")
	bool bShowEmptySlots = true;

	/** Slot viewmodels currently shown, in layout order (a window of the pool). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInvUI_SlotViewModel>> ActiveSlotVMs;

	/** Reusable pool of slot viewmodels (kept alive across rebuilds to avoid widget churn). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UInvUI_SlotViewModel>> SlotVMPool;

	/** Index of the next free pool entry during a rebuild. */
	int32 PoolCursor = 0;

	// --- Observable backing fields ---
	UPROPERTY(Transient) FInvUI_ContainerInstanceId ContainerId;
	UPROPERTY(Transient) int32 StructureRevision = 0;
	UPROPERTY(Transient) int32 ColumnCount = 0;
	UPROPERTY(Transient) int32 RowCount = 0;
	UPROPERTY(Transient) bool bBound = false;

	/** Streamable manager owning this vm's async icon loads. */
	FStreamableManager StreamableManager;

	/** Per-slot in-flight icon stream handles, keyed by the slot vm. */
	TMap<TWeakObjectPtr<UInvUI_SlotViewModel>, TSharedPtr<FStreamableHandle>> ActiveStreams;
};
