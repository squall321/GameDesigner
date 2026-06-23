// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Binding/DPViewBindingLibrary.h"
#include "Binding/DPViewModelBinder.h"
#include "Blueprint/UserWidget.h"

UDP_ViewModelBinder* UDP_ViewBindingLibrary::CreateBinder(UUserWidget* Owner)
{
	if (!Owner)
	{
		return nullptr;
	}
	// Outer to the owning widget so the binder shares its lifetime and is GC-kept by it.
	return NewObject<UDP_ViewModelBinder>(Owner);
}

UDP_ViewModelBinder* UDP_ViewBindingLibrary::BindFieldToWidget(UUserWidget* Owner, UDP_ViewModelBase* ViewModel,
	FName FieldName, UWidget* TargetWidget, FName SetterName)
{
	UDP_ViewModelBinder* Binder = CreateBinder(Owner);
	if (!Binder)
	{
		return nullptr;
	}

	if (!Binder->BindField(ViewModel, FieldName, TargetWidget, SetterName))
	{
		// Validation failed — drop the binder (let it GC) so callers get a clean null on failure.
		return nullptr;
	}
	return Binder;
}
