// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/FieldId.h"

// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4 and is merged into
// CoreUObject in 5.5+. Include the right header for the engine band.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "DPViewBase.generated.h"

class UDP_ViewModelBase;

/**
 * Base class for MVVM views in the DesignPatterns UI module.
 *
 * A view is a "dumb" UUserWidget: it holds exactly one ViewModel
 * (UDP_ViewModelBase), binds to that ViewModel's field-changed notifications on
 * NativeConstruct, and UNBINDS deterministically on NativeDestruct (with a
 * BeginDestroy backstop so a widget torn down without NativeDestruct still
 * releases its bindings).
 *
 * Views communicate UPWARD only through intents published on the CORE message
 * bus (PublishIntent) — they never hold gameplay pointers and never call into
 * gameplay systems directly. This keeps the UI a pure projection of state +
 * a source of tagged intents that the mediator/game systems route.
 */
UCLASS(Abstract, Blueprintable, meta = (DisplayName = "DP View Base"))
class DESIGNPATTERNSUI_API UDP_ViewBase : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * Assign (or replace) this view's ViewModel. Unbinds from the previous one,
	 * binds to the new one if this widget is constructed, and fires OnViewModelSet.
	 * Safe to call before or after NativeConstruct.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|View")
	void SetViewModel(UDP_ViewModelBase* InViewModel);

	/** The currently-bound ViewModel, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|View")
	UDP_ViewModelBase* GetViewModel() const { return ViewModel; }

	/**
	 * Publish an intent on the core message bus. This is the ONLY upward channel
	 * a view should use. Resolves the bus from the game instance; no-op if the
	 * bus is unavailable (e.g. design-time/editor preview).
	 *
	 * @param IntentChannel  GameplayTag channel the intent is published on.
	 * @param Payload        Optional typed payload (Make Instanced Struct).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|View",
		meta = (AdvancedDisplay = "Payload"))
	void PublishIntent(FGameplayTag IntentChannel, FInstancedStruct Payload);

protected:
	//~ Begin UUserWidget
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	//~ End UUserWidget

	//~ Begin UObject
	virtual void BeginDestroy() override;
	//~ End UObject

	/**
	 * Designer hook fired whenever a (non-null) ViewModel becomes the active one.
	 * Implement in Blueprint to do the initial read of ViewModel state into widgets.
	 */
	UFUNCTION(BlueprintImplementableEvent, Category = "DesignPatterns|View",
		meta = (DisplayName = "On View Model Set"))
	void OnViewModelSet(UDP_ViewModelBase* NewViewModel);

	/**
	 * Designer-overridable reaction to any field change on the bound ViewModel.
	 * The base implementation is empty; override to re-read specific fields.
	 * Native code can also override OnFieldValueChanged for typed handling.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|View",
		meta = (DisplayName = "On View Model Field Changed"))
	void OnViewModelFieldChanged(FName FieldName);
	virtual void OnViewModelFieldChanged_Implementation(FName FieldName);

	/** The single ViewModel this view projects. Owning ref so it is GC-kept while bound. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|View",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDP_ViewModelBase> ViewModel = nullptr;

private:
	/** Bind to every observable field on the current ViewModel. Idempotent. */
	void BindToViewModel();

	/** Remove all field bindings from the current ViewModel. Idempotent. */
	void UnbindFromViewModel();

	/** Routed from the ViewModel's field-changed multicast; forwards to OnViewModelFieldChanged. */
	void HandleFieldValueChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);

	/** True between successful BindToViewModel and UnbindFromViewModel — guards double bind/unbind. */
	bool bBoundToViewModel = false;
};
