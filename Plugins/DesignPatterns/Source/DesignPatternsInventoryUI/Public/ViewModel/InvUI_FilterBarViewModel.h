// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/IClassDescriptor.h"
#include "GameplayTagContainer.h"
#include "InvUI_FilterBarViewModel.generated.h"

class UInvUI_GridViewModel;
class UInvUI_SortStrategy;
class UInvUI_SortBySearchRelevance;

/**
 * Backs the sort/filter/search bar above a grid.
 *
 * Observable SearchText / ActiveTypeFilter / ActiveSortMode. On change it drives the bound
 * UInvUI_GridViewModel through its EXISTING SetSortStrategy / SetItemFilter / SetShowEmptySlots +
 * Rebuild — it never touches the backend. The search text feeds a UInvUI_SortBySearchRelevance the
 * bar owns; the type filter feeds the grid VM's hierarchy-aware tag filter. The chosen sort mode
 * and show-empty preference are persisted to UInvUI_Settings keyed by the container's KindTag, so
 * a player's per-kind choices survive a session.
 */
UCLASS(BlueprintType, meta = (DisplayName = "InvUI Filter Bar ViewModel"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_FilterBarViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	UInvUI_FilterBarViewModel();

	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		SearchText = 0,
		ActiveTypeFilter,
		ActiveSortMode,
		bShowEmpty,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/**
	 * Bind to the grid VM this bar controls and the KindTag (for preference persistence). Loads the
	 * saved sort/show-empty preference for that kind and applies it. SortModeStrategyMap maps a
	 * sort-mode tag to a concrete sort strategy instance the bar can install on the grid VM; supply
	 * it from the window/data so this VM stays free of concrete strategy choices.
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void BindGrid(UInvUI_GridViewModel* InGridViewModel, FGameplayTag InKindTag);

	/** Register a selectable sort mode -> strategy instance (called by the window during setup). */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void RegisterSortMode(FGameplayTag SortModeTag, UInvUI_SortStrategy* Strategy);

	/** Set the free-text search; updates the relevance sort and rebuilds the grid. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void SetSearchText(FText InSearchText);

	/** Set the hierarchy-aware type filter; drives the grid VM's SetItemFilter and rebuilds. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void SetTypeFilter(const FGameplayTagContainer& InFilter);

	/** Select a sort mode (installs its strategy on the grid VM); persists the choice. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void SetSortMode(FGameplayTag InSortMode);

	/** Toggle empty-slot visibility on the grid VM; persists the choice. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void SetShowEmpty(bool bInShowEmpty);

	/** Persist the current sort-mode + show-empty choices into UInvUI_Settings for the bound kind. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void SavePreference();

	/** Load (and apply) the saved sort-mode + show-empty for KindTag. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void LoadPreference(FGameplayTag KindTag);

	// --- Observable getters ---
	UFUNCTION(BlueprintPure, Category = "InvUI|FilterBar") FText GetSearchText() const { return SearchText; }
	UFUNCTION(BlueprintPure, Category = "InvUI|FilterBar") FGameplayTagContainer GetActiveTypeFilter() const { return ActiveTypeFilter; }
	UFUNCTION(BlueprintPure, Category = "InvUI|FilterBar") FGameplayTag GetActiveSortMode() const { return ActiveSortMode; }
	UFUNCTION(BlueprintPure, Category = "InvUI|FilterBar") bool GetShowEmpty() const { return bShowEmpty; }

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

private:
	void BroadcastField(EField Field);

	/** Push the active sort mode's strategy (and the search relevance term) onto the grid VM. */
	void ApplySortToGrid();

	/** The grid VM this bar drives (owning ref kept alive while bound). */
	UPROPERTY(Transient)
	TObjectPtr<UInvUI_GridViewModel> GridViewModel = nullptr;

	/** Container kind used as the preference key. */
	UPROPERTY(Transient)
	FGameplayTag KindTag;

	/** Sort-mode tag -> installable strategy instance, supplied by the window. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, TObjectPtr<UInvUI_SortStrategy>> SortStrategies;

	/** Dedicated relevance sort the bar owns; the search text feeds it when active. */
	UPROPERTY(Transient)
	TObjectPtr<UInvUI_SortBySearchRelevance> SearchSort = nullptr;

	// --- Observable backing fields ---
	UPROPERTY(Transient) FText SearchText;
	UPROPERTY(Transient) FGameplayTagContainer ActiveTypeFilter;
	UPROPERTY(Transient) FGameplayTag ActiveSortMode;
	UPROPERTY(Transient) bool bShowEmpty = true;
};
