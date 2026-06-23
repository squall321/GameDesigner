// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "UObject/ScriptInterface.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/IClassDescriptor.h"
#include "GameplayTagContainer.h"
#include "Seam/InvUI_ItemContainer.h"
#include "Stats/Seam_ItemStats.h"
#include "InvUI_ComparisonViewModel.generated.h"

class ISeam_ItemStats;

/**
 * One row of an item-comparison readout: how a hovered item's contribution to an attribute differs
 * from the currently equipped item's. Delta = Hovered - Equipped; bIsUpgrade is a coarse "higher is
 * better" hint (true when Delta > 0). All seam-neutral primitives so it can cross to BP/widgets.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSINVENTORYUI_API FInvUI_StatDelta
{
	GENERATED_BODY()

	/** The attribute this row compares. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Comparison")
	FGameplayTag Attribute;

	/** The hovered item's net contribution to the attribute. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Comparison")
	float Hovered = 0.f;

	/** The equipped item's net contribution to the attribute. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Comparison")
	float Equipped = 0.f;

	/** Hovered - Equipped. */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Comparison")
	float Delta = 0.f;

	/** Coarse "this is an upgrade" hint (Delta > 0). */
	UPROPERTY(BlueprintReadOnly, Category = "InvUI|Comparison")
	bool bIsUpgrade = false;

	FInvUI_StatDelta() = default;
};

/**
 * Drives the compare-tooltip. DRIVEN BY THE WINDOW, not the slot widget (the slot's
 * MakeTooltipWidget is a param-less BP event). The window pushes the hovered (container, slot) and
 * the equipped (container, slot) it knows from the registry; the VM reads both item tags through
 * IInvUI_ItemContainer::Execute_GetSlot, resolves each item's FSeam_ItemStatSet through the
 * ISeam_ItemStats seam (sync-cached fast path, async fallback), and diffs them into per-attribute
 * FInvUI_StatDelta rows. Hand-rolled FieldNotification; holds only seams; never mutates anything.
 */
UCLASS(BlueprintType, meta = (DisplayName = "InvUI Comparison ViewModel"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_ComparisonViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	UInvUI_ComparisonViewModel();

	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		HoveredItem = 0,
		EquippedItem,
		DeltaRows,
		bHasComparison,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/**
	 * Supply the ISeam_ItemStats resolver (usually resolved by the window from the service locator
	 * under InvUITags::Service_ItemStats). Held weakly via TScriptInterface (kept alive by its owner).
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Comparison")
	void SetStatResolver(const TScriptInterface<ISeam_ItemStats>& InResolver);

	/** Set the hovered (container, slot) and recompute. Reads the item tag via Execute_GetSlot. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Comparison")
	void SetHovered(const TScriptInterface<IInvUI_ItemContainer>& Container, FGameplayTag SlotTag);

	/** Set the equipped baseline (container, slot) the hovered item is compared against, and recompute. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Comparison")
	void SetEquippedContainer(const TScriptInterface<IInvUI_ItemContainer>& Container, FGameplayTag EquipSlot);

	/** Clear both sides and the delta rows. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Comparison")
	void Clear();

	// --- Observable getters ---

	/** The hovered item tag (invalid when none). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Comparison") FGameplayTag GetHoveredItem() const { return HoveredItem; }
	/** The equipped baseline item tag (invalid when none). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Comparison") FGameplayTag GetEquippedItem() const { return EquippedItem; }
	/** The per-attribute delta rows (copied for BP safety). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Comparison") TArray<FInvUI_StatDelta> GetDeltaRows() const { return DeltaRows; }
	/** True once both sides have been resolved into rows. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Comparison") bool HasComparison() const { return bHasComparison; }

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Recompute DeltaRows from the cached hovered/equipped stat sets when both are resolved. */
	void RebuildDeltas();

	/**
	 * Resolve ItemTag's stats: try the synchronous cached path, else kick the async path with a
	 * weak-this callback that re-runs RebuildDeltas. OutSet/bOutResolved hold the sync result.
	 */
	void ResolveSide(const FGameplayTag& ItemTag, bool bHoveredSide);

	/** Read the item tag of SlotTag in Container (invalid if empty/absent). */
	static FGameplayTag ReadItemTag(const TScriptInterface<IInvUI_ItemContainer>& Container, const FGameplayTag& SlotTag);

	/** Async-resolve sink for the hovered side. */
	UFUNCTION()
	void OnHoveredStatsResolved(const FSeam_ItemStatSet& Stats);

	/** Async-resolve sink for the equipped side. */
	UFUNCTION()
	void OnEquippedStatsResolved(const FSeam_ItemStatSet& Stats);

	/** The stat resolver seam (kept alive by its owner). */
	UPROPERTY(Transient)
	TScriptInterface<ISeam_ItemStats> StatResolver;

	/** Cached resolved stat sets for each side. */
	FSeam_ItemStatSet HoveredStats;
	FSeam_ItemStatSet EquippedStats;
	bool bHoveredResolved = false;
	bool bEquippedResolved = false;

	// --- Observable backing fields ---
	UPROPERTY(Transient) FGameplayTag HoveredItem;
	UPROPERTY(Transient) FGameplayTag EquippedItem;
	UPROPERTY(Transient) TArray<FInvUI_StatDelta> DeltaRows;
	UPROPERTY(Transient) bool bHasComparison = false;
};
