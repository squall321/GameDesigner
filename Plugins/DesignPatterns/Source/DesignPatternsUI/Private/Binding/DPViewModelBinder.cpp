// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Binding/DPViewModelBinder.h"
#include "MVVM/DPViewModelBase.h"
#include "Core/DPLog.h"

#include "Components/Widget.h"
#include "INotifyFieldValueChanged.h"
#include "FieldNotification/IClassDescriptor.h"
#include "UObject/UnrealType.h"

bool UDP_ViewModelBinder::BindField(UDP_ViewModelBase* ViewModel, FName FieldName, UWidget* Widget, FName SetterName)
{
	if (!ViewModel || !Widget || FieldName.IsNone() || SetterName.IsNone())
	{
		UE_LOG(LogDP, Warning, TEXT("[Binder] BindField called with a null/empty argument."));
		return false;
	}

	// A single binder routes from one ViewModel. Binding a second VM is a usage error.
	if (BoundViewModel && BoundViewModel != ViewModel)
	{
		UE_LOG(LogDP, Warning,
			TEXT("[Binder] BindField: this binder is already bound to a different ViewModel (%s); ignoring."),
			*BoundViewModel->GetName());
		return false;
	}

	// Resolve the field's backing property and validate the setter against it before committing.
	const FProperty* SourceProperty = ResolveFieldProperty(ViewModel, FieldName);
	if (!SourceProperty)
	{
		UE_LOG(LogDP, Warning, TEXT("[Binder] BindField: '%s' is not a property/field on %s; skipping."),
			*FieldName.ToString(), *ViewModel->GetClass()->GetName());
		return false;
	}

	if (!ValidateSetterSignature(Widget, SetterName, SourceProperty))
	{
		UE_LOG(LogDP, Warning,
			TEXT("[Binder] BindField: setter '%s' on %s is not a single-param function assignable from field '%s'; skipping."),
			*SetterName.ToString(), *Widget->GetClass()->GetName(), *FieldName.ToString());
		return false;
	}

	AttachToViewModel(ViewModel);

	FDP_FieldBinding Binding;
	Binding.FieldName = FieldName;
	Binding.TargetWidget = Widget;
	Binding.SetterFunctionName = SetterName;
	Bindings.Add(Binding);

	// Push the current value immediately so the widget starts in sync.
	InvokeSetterWithFieldValue(Widget, SetterName, ViewModel, SourceProperty);
	return true;
}

void UDP_ViewModelBinder::AttachToViewModel(UDP_ViewModelBase* ViewModel)
{
	BoundViewModel = ViewModel;
	if (bAttached || !BoundViewModel)
	{
		return;
	}

	// Register ONE routing delegate to every observable field, keyed to THIS binder as the user
	// object — so we never collide with the view's RemoveAllFieldValueChangedDelegates(this).
	const UE::FieldNotification::IClassDescriptor& Descriptor = BoundViewModel->GetFieldNotificationDescriptor();

	INotifyFieldValueChanged::FFieldValueChangedDelegate Delegate =
		INotifyFieldValueChanged::FFieldValueChangedDelegate::CreateUObject(this, &UDP_ViewModelBinder::HandleFieldChanged);

	Descriptor.ForEachField(BoundViewModel->GetClass(), [this, &Delegate](UE::FieldNotification::FFieldId Field)
	{
		BoundViewModel->AddFieldValueChangedDelegate(Field, Delegate);
		return true;
	});

	bAttached = true;
}

void UDP_ViewModelBinder::HandleFieldChanged(UObject* /*Object*/, UE::FieldNotification::FFieldId FieldId)
{
	if (FieldId.IsValid())
	{
		PushFieldToBindings(FieldId.GetName());
	}
}

void UDP_ViewModelBinder::PushFieldToBindings(FName FieldName)
{
	if (!BoundViewModel)
	{
		return;
	}

	const FProperty* SourceProperty = ResolveFieldProperty(BoundViewModel, FieldName);
	if (!SourceProperty)
	{
		return;
	}

	for (const FDP_FieldBinding& Binding : Bindings)
	{
		if (Binding.FieldName != FieldName)
		{
			continue;
		}
		if (UWidget* Widget = Binding.TargetWidget.Get())
		{
			InvokeSetterWithFieldValue(Widget, Binding.SetterFunctionName, BoundViewModel, SourceProperty);
		}
	}
}

const FProperty* UDP_ViewModelBinder::ResolveFieldProperty(const UDP_ViewModelBase* ViewModel, FName FieldName)
{
	if (!ViewModel)
	{
		return nullptr;
	}

	// Prefer the FieldNotification descriptor — confirm the name is a registered observable field.
	bool bIsObservableField = false;
	const UE::FieldNotification::IClassDescriptor& Descriptor = ViewModel->GetFieldNotificationDescriptor();
	Descriptor.ForEachField(ViewModel->GetClass(), [&bIsObservableField, FieldName](UE::FieldNotification::FFieldId Field)
	{
		if (Field.GetName() == FieldName)
		{
			bIsObservableField = true;
			return false; // stop
		}
		return true;
	});

	// Whether or not it is a registered FieldNotify field, the value lives in a reflected property.
	// FindFProperty is the fallback for designer SetProperty fields not tagged FieldNotify.
	const FProperty* Property = FindFProperty<FProperty>(ViewModel->GetClass(), FieldName);

	if (!bIsObservableField && Property)
	{
		UE_LOG(LogDP, Verbose,
			TEXT("[Binder] Field '%s' on %s is a plain property (not FieldNotify-tagged); bound via property fallback (won't auto-update unless broadcast)."),
			*FieldName.ToString(), *ViewModel->GetClass()->GetName());
	}

	return Property;
}

bool UDP_ViewModelBinder::ValidateSetterSignature(const UWidget* Widget, FName SetterName, const FProperty* SourceProperty)
{
	if (!Widget || !SourceProperty)
	{
		return false;
	}

	const UFunction* Setter = Widget->FindFunction(SetterName);
	if (!Setter)
	{
		return false;
	}

	// Count the parameters (excluding return value) — we require exactly one input param.
	int32 ParamCount = 0;
	const FProperty* FirstParam = nullptr;
	for (TFieldIterator<FProperty> It(Setter); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		if (It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			continue;
		}
		if (ParamCount == 0)
		{
			FirstParam = *It;
		}
		++ParamCount;
	}

	if (ParamCount != 1 || !FirstParam)
	{
		return false;
	}

	// The param must be the same property class as (and assignable from) the source field type.
	// SameType covers identical numeric/struct/object types; this is the practical compatibility gate.
	return FirstParam->SameType(SourceProperty);
}

void UDP_ViewModelBinder::InvokeSetterWithFieldValue(UWidget* Widget, FName SetterName,
	const UDP_ViewModelBase* ViewModel, const FProperty* SourceProperty)
{
	if (!Widget || !ViewModel || !SourceProperty)
	{
		return;
	}

	UFunction* Setter = Widget->FindFunction(SetterName);
	if (!Setter)
	{
		return;
	}

	// Find the single input param to copy the field value into.
	FProperty* ParamProperty = nullptr;
	for (TFieldIterator<FProperty> It(Setter); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		if (!It->HasAnyPropertyFlags(CPF_ReturnParm))
		{
			ParamProperty = *It;
			break;
		}
	}
	if (!ParamProperty || !ParamProperty->SameType(SourceProperty))
	{
		return;
	}

	// Build a parameter frame, copy the VM field value into the param slot, and invoke.
	void* ParamBuffer = FMemory_Alloca(Setter->ParmsSize);
	FMemory::Memzero(ParamBuffer, Setter->ParmsSize);
	Setter->InitializeStruct(ParamBuffer);

	const void* SourceValue = SourceProperty->ContainerPtrToValuePtr<void>(ViewModel);
	void* DestValue = ParamProperty->ContainerPtrToValuePtr<void>(ParamBuffer);
	ParamProperty->CopyCompleteValue(DestValue, SourceValue);

	Widget->ProcessEvent(Setter, ParamBuffer);

	Setter->DestroyStruct(ParamBuffer);
}

void UDP_ViewModelBinder::UnbindAll()
{
	if (bAttached && BoundViewModel)
	{
		// Drop only OUR delegates (keyed to this binder) — never touch the view's bindings.
		BoundViewModel->RemoveAllFieldValueChangedDelegates(this);
	}
	bAttached = false;
	Bindings.Reset();
	BoundViewModel = nullptr;
}

void UDP_ViewModelBinder::BeginDestroy()
{
	UnbindAll();
	Super::BeginDestroy();
}
