// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "FieldNotification/FieldId.h"
#include "GameplayTagContainer.h"
#include "InvUI_FilterBarWidget.generated.h"

class UInvUI_FilterBarViewModel;

/**
 * Skinnable sort/filter/search bar view. C++ owns the binding; the BP owns the search box, sort
 * dropdown and filter chips. The widget forwards designer input (SetSearchText/SetTypeFilter/
 * SetSortMode/SetShowEmpty) to the bound UInvUI_FilterBarViewModel — which drives the grid VM and
 * persists preferences — and repaints on VM changes via OnFilterBarRefreshed.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "InvUI Filter Bar Widget"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_FilterBarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Bind the filter-bar VM and do an initial refresh. Unbinds any previous VM first. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void BindFilterBar(UInvUI_FilterBarViewModel* InViewModel);

	/** Detach from the current VM. Idempotent. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar")
	void UnbindFilterBar();

	/** The bound filter-bar VM, or null. */
	UFUNCTION(BlueprintPure, Category = "InvUI|FilterBar")
	UInvUI_FilterBarViewModel* GetFilterBarViewModel() const { return ViewModel; }

	// --- Designer input forwarders ---
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar") void SetSearchText(FText InSearchText);
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar") void SetTypeFilter(const FGameplayTagContainer& InFilter);
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar") void SetSortMode(FGameplayTag InSortMode);
	UFUNCTION(BlueprintCallable, Category = "InvUI|FilterBar") void SetShowEmpty(bool bInShowEmpty);

protected:
	//~ Begin UUserWidget
	virtual void NativeDestruct() override;
	//~ End UUserWidget

	/** Designer hook fired on every VM change so the BP repaints the current search/sort/filter state. */
	UFUNCTION(BlueprintImplementableEvent, Category = "InvUI|FilterBar", meta = (DisplayName = "On Filter Bar Refreshed"))
	void OnFilterBarRefreshed();

private:
	void HandleViewModelFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);
	void BindViewModelDelegates();
	void UnbindViewModelDelegates();

	/** The filter-bar VM this widget projects (owning ref keeps it alive while bound). */
	UPROPERTY(Transient)
	TObjectPtr<UInvUI_FilterBarViewModel> ViewModel = nullptr;

	bool bBoundToViewModel = false;
};
