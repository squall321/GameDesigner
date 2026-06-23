// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FieldNotification/FieldId.h"
#include "DPViewModelBinder.generated.h"

class UDP_ViewModelBase;
class UWidget;

/**
 * One live binding: a ViewModel field -> a setter UFUNCTION on a target widget.
 *
 * When the bound field broadcasts a change, the binder reads the field's current value off the
 * ViewModel and calls the named setter on the widget with it. The widget + field are held weakly /
 * by id so a binding never keeps either side alive.
 */
USTRUCT()
struct DESIGNPATTERNSUI_API FDP_FieldBinding
{
	GENERATED_BODY()

	/** The observable field on the ViewModel. */
	UPROPERTY()
	FName FieldName;

	/** The target widget the value is pushed to. */
	UPROPERTY()
	TWeakObjectPtr<UWidget> TargetWidget;

	/** The setter UFUNCTION name on the target widget (single param matching the field type). */
	UPROPERTY()
	FName SetterFunctionName;
};

/**
 * One-call VM-field-to-widget binder.
 *
 * The core UDP_ViewBase binds a SINGLE delegate to every field and routes changes to one virtual
 * (OnViewModelFieldChanged) — great for code-driven views, but verbose for designers who just want
 * "field X drives setter Y on widget Z". This binder closes that gap:
 *
 *   Binder->BindField(VM, "Health", HealthBar, "SetPercent");
 *
 * On bind it validates that the setter exists and takes exactly one parameter assignable from the
 * field's property type (skipping + warning on mismatch). It registers its delegates AS THE BINDER
 * (not as the view) so it never collides with the view's own RemoveAllFieldValueChangedDelegates(this),
 * and it auto-unbinds on destruct.
 *
 * Field resolution prefers the ViewModel's FieldNotification class descriptor (the proper observable
 * path) and FALLS BACK to FindFProperty for designer SetProperty-driven fields that were not tagged
 * FieldNotify, so both authoring styles work.
 */
UCLASS(BlueprintType)
class DESIGNPATTERNSUI_API UDP_ViewModelBinder : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Bind a ViewModel field to a setter on a widget. Pushes the current value immediately so the
	 * widget starts in sync. Returns true if the binding was established (false on validation fail).
	 *
	 * @param ViewModel  The source ViewModel (also becomes this binder's bound model).
	 * @param FieldName  The observable field name on the ViewModel.
	 * @param Widget     The target widget.
	 * @param SetterName The single-param setter UFUNCTION on the widget.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Binding")
	bool BindField(UDP_ViewModelBase* ViewModel, FName FieldName, UWidget* Widget, FName SetterName);

	/** Remove every binding and detach from the ViewModel. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Binding")
	void UnbindAll();

	/** The ViewModel this binder is attached to, or null. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Binding")
	UDP_ViewModelBase* GetBoundViewModel() const { return BoundViewModel; }

	//~ Begin UObject
	virtual void BeginDestroy() override;
	//~ End UObject

private:
	/** Attach our single routing delegate to every observable field of ViewModel (idempotent). */
	void AttachToViewModel(UDP_ViewModelBase* ViewModel);

	/** Routed from the ViewModel field-changed multicast — push the changed field to its bindings. */
	void HandleFieldChanged(UObject* Object, UE::FieldNotification::FFieldId FieldId);

	/** Read FieldName off BoundViewModel and call the setter on each binding for that field. */
	void PushFieldToBindings(FName FieldName);

	/** Validate that Setter on Widget takes exactly one param assignable from SourceProperty's type. */
	static bool ValidateSetterSignature(const UWidget* Widget, FName SetterName, const FProperty* SourceProperty);

	/** Resolve the FProperty backing FieldName on ViewModel (FieldNotify descriptor, else FindFProperty). */
	static const FProperty* ResolveFieldProperty(const UDP_ViewModelBase* ViewModel, FName FieldName);

	/** Copy the value of SourceProperty (on the VM) into a transient param buffer and invoke Setter. */
	static void InvokeSetterWithFieldValue(UWidget* Widget, FName SetterName,
		const UDP_ViewModelBase* ViewModel, const FProperty* SourceProperty);

	/** The ViewModel this binder routes from. */
	UPROPERTY()
	TObjectPtr<UDP_ViewModelBase> BoundViewModel = nullptr;

	/** All field->setter bindings. */
	UPROPERTY()
	TArray<FDP_FieldBinding> Bindings;

	/** True once our routing delegate is attached to BoundViewModel. */
	bool bAttached = false;
};
