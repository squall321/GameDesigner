// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "INotifyFieldValueChanged.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/IClassDescriptor.h"
#include "FieldNotification/FieldMulticastDelegate.h"
#include "DPViewModelBase.generated.h"

/**
 * Lite ViewModel base built directly on the engine's FieldNotification system
 * (INotifyFieldValueChanged), deliberately NOT depending on the optional
 * ModelViewViewModel plugin (WITH_DP_MVVM=0).
 *
 * A ViewModel is a plain UObject that exposes observable properties. Views
 * (UDP_ViewBase) bind to its field-changed delegate and re-read values when a
 * field broadcasts. ViewModels hold NO gameplay pointers and never reach into
 * the world directly: they are pushed data by their owner/mediator and they
 * raise field changes when that data mutates.
 *
 * Authoring a derived ViewModel:
 *  1. Declare a UPROPERTY with meta=(AllowPrivateAccess) and a BlueprintCallable
 *     getter marked meta=(BlueprintGetter) / FieldNotify so it is observable.
 *  2. In the setter call SetProperty(Field, NewValue) — it stores the value and
 *     broadcasts the field change only when the value actually differs.
 *
 * Because UE's header tool generates the field descriptor for properties tagged
 * with the FieldNotify metadata, derived classes typically use the
 * UE_FIELD_NOTIFICATION_* helpers via the GENERATED_BODY machinery. This base
 * additionally exposes a hand-rolled SetProperty<T> + BroadcastFieldValueChanged
 * pair so designers can drive change notifications without that boilerplate.
 */
UCLASS(Abstract, Blueprintable, BlueprintType, meta = (DisplayName = "DP ViewModel Base"))
class DESIGNPATTERNSUI_API UDP_ViewModelBase : public UObject, public INotifyFieldValueChanged
{
	GENERATED_BODY()

public:
	//~ Begin INotifyFieldValueChanged
	virtual FDelegateHandle AddFieldValueChangedDelegate(
		UE::FieldNotification::FFieldId InFieldId,
		FFieldValueChangedDelegate InNewDelegate) override;

	virtual bool RemoveFieldValueChangedDelegate(
		UE::FieldNotification::FFieldId InFieldId,
		FDelegateHandle InHandle) override;

	virtual int32 RemoveAllFieldValueChangedDelegates(const void* InUserObject) override;

	virtual int32 RemoveAllFieldValueChangedDelegates(
		UE::FieldNotification::FFieldId InFieldId,
		const void* InUserObject) override;

	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/**
	 * Blueprint-callable broadcast for designer-authored ViewModels. Resolves the
	 * named FieldNotify property on this class and broadcasts its change so any
	 * bound view re-reads it. No-op (with a warning) if the name is not a
	 * registered field on this class.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|MVVM",
		meta = (DisplayName = "Broadcast Field Value Changed"))
	void K2_BroadcastFieldValueChanged(FName FieldName);

protected:
	/**
	 * Broadcast that a field changed to every delegate registered for that field.
	 * Call this after you have stored the new value. Prefer SetProperty<T> which
	 * does the store + equality check + broadcast for you.
	 */
	void BroadcastFieldValueChanged(UE::FieldNotification::FFieldId InFieldId);

	/**
	 * Store-and-notify helper. Assigns NewValue to ValueRef only if it differs
	 * (operator!= must exist for T), then broadcasts InFieldId. Returns true when
	 * a change was made — useful for chaining derived-field updates.
	 *
	 * @param InFieldId  The FieldNotification id for the property being set.
	 * @param ValueRef   Reference to the backing storage to update.
	 * @param NewValue   The candidate new value.
	 */
	template <typename T>
	bool SetProperty(UE::FieldNotification::FFieldId InFieldId, T& ValueRef, const T& NewValue)
	{
		if (ValueRef == NewValue)
		{
			return false;
		}
		ValueRef = NewValue;
		BroadcastFieldValueChanged(InFieldId);
		return true;
	}

private:
	/** Per-field multicast registry of bound view delegates. */
	UE::FieldNotification::FFieldMulticastDelegate FieldNotifyDelegates;

	/** Bitset tracking which fields currently have at least one bound delegate. */
	TBitArray<> EnabledFieldNotifications;
};
