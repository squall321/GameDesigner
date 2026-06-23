// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DPViewBindingLibrary.generated.h"

class UDP_ViewModelBase;
class UDP_ViewModelBinder;
class UWidget;
class UUserWidget;

/**
 * One-call helpers for VM-field-to-widget binding.
 *
 * The binder object (UDP_ViewModelBinder) does the work; this library is the ergonomic front door
 * so a screen can wire a field to a widget setter in a single Blueprint/C++ call without manually
 * creating + owning a binder. The created binder is OUTERED to the owning widget so it shares the
 * widget's lifetime and is GC'd with it, and it auto-unbinds on destroy.
 */
UCLASS()
class DESIGNPATTERNSUI_API UDP_ViewBindingLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Create a binder owned by Owner and bind ViewModel.FieldName -> TargetWidget.SetterName in one
	 * call. Returns the binder (so the caller can add more bindings / keep a reference); the binder
	 * is GC-kept by Owner. Returns null if the initial bind fails validation.
	 *
	 * @param Owner        The widget that will own (outer) the binder — typically the view doing the binding.
	 * @param ViewModel    The source ViewModel.
	 * @param FieldName    The observable field name on the ViewModel.
	 * @param TargetWidget The widget whose setter receives the value.
	 * @param SetterName   The single-param setter UFUNCTION on TargetWidget.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Binding")
	static UDP_ViewModelBinder* BindFieldToWidget(UUserWidget* Owner, UDP_ViewModelBase* ViewModel,
		FName FieldName, UWidget* TargetWidget, FName SetterName);

	/**
	 * Create a binder owned by Owner without binding anything yet — for callers that want to add a
	 * batch of bindings via UDP_ViewModelBinder::BindField themselves.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Binding")
	static UDP_ViewModelBinder* CreateBinder(UUserWidget* Owner);
};
