// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "UObject/ScriptInterface.h"
#include "FieldNotification/FieldId.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Seam/InvUI_ItemContainer.h"
#include "InvUI_EncumbranceViewModel.generated.h"

/**
 * Observable weight/volume/capacity totals for a SPATIAL container.
 *
 * Binds to one IInvUI_ItemContainer (through its dynamic change delegate) and, on every change,
 * sums the FInvUI_SpatialFootprint.Weight/Volume carried in each occupied slot's ItemPayload. The
 * max weight/volume are designer tunables (no magic numbers) and an item count is exposed for a
 * simple "12 / 30 slots" readout. Hand-rolled FieldNotification, matching UInvUI_SlotViewModel /
 * UInvUI_GridViewModel exactly; holds only the seam (TScriptInterface), no gameplay pointers.
 */
UCLASS(BlueprintType, meta = (DisplayName = "InvUI Encumbrance ViewModel"))
class DESIGNPATTERNSINVENTORYUI_API UInvUI_EncumbranceViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	UInvUI_EncumbranceViewModel();

	/** Stable, ordered ids for this viewmodel's observable fields. */
	enum class EField : int32
	{
		CurrentWeight = 0,
		MaxWeight,
		CurrentVolume,
		MaxVolume,
		ItemCount,
		bOverweight,
		Num
	};

	//~ Begin UObject
	virtual void BeginDestroy() override;
	//~ End UObject

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/**
	 * Bind to Container and recompute. Subscribes to the seam's dynamic change delegate so the totals
	 * track backend changes. Re-binding first unbinds the previous container. Container may be empty
	 * (clears the totals).
	 */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Encumbrance")
	void BindContainer(const TScriptInterface<IInvUI_ItemContainer>& Container);

	/** Unbind the current container and zero the totals. Safe if not bound. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Encumbrance")
	void UnbindContainer();

	/** Recompute the totals from the bound container's current slots (normally automatic). */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Encumbrance")
	void Recompute();

	/** Designer-tunable carrying limits (no hardcoded values). 0 = "no limit" for that axis. */
	UFUNCTION(BlueprintCallable, Category = "InvUI|Encumbrance")
	void SetLimits(float InMaxWeight, float InMaxVolume);

	// --- Observable getters ---

	/** Total weight of carried items. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Encumbrance") float GetCurrentWeight() const { return CurrentWeight; }
	/** Maximum weight (0 = unlimited). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Encumbrance") float GetMaxWeight() const { return MaxWeight; }
	/** Total volume of carried items. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Encumbrance") float GetCurrentVolume() const { return CurrentVolume; }
	/** Maximum volume (0 = unlimited). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Encumbrance") float GetMaxVolume() const { return MaxVolume; }
	/** Number of occupied slots counted. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Encumbrance") int32 GetItemCount() const { return ItemCount; }
	/** True when CurrentWeight exceeds a non-zero MaxWeight. */
	UFUNCTION(BlueprintPure, Category = "InvUI|Encumbrance") bool IsOverweight() const { return bOverweight; }
	/** Weight fill fraction in [0,1] (0 when unlimited). */
	UFUNCTION(BlueprintPure, Category = "InvUI|Encumbrance") float GetWeightFraction() const;

	/** Resolve the FFieldId for one of this viewmodel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Recompute bOverweight from the current weight/limit and broadcast if it changed. */
	void UpdateOverweight();

	/** Dynamic handler bound to the seam's change delegate; triggers a recompute. */
	UFUNCTION()
	void HandleContainerChanged(const FInvUI_ContainerInstanceId& InContainerId);

	/** The bound container seam (kept alive by its real owner). */
	UPROPERTY(Transient)
	TScriptInterface<IInvUI_ItemContainer> BoundContainer;

	/** True while subscribed to the seam delegate. */
	bool bSubscribed = false;

	// --- Observable backing fields ---
	UPROPERTY(Transient) float CurrentWeight = 0.f;
	UPROPERTY(Transient) float MaxWeight = 0.f;
	UPROPERTY(Transient) float CurrentVolume = 0.f;
	UPROPERTY(Transient) float MaxVolume = 0.f;
	UPROPERTY(Transient) int32 ItemCount = 0;
	UPROPERTY(Transient) bool bOverweight = false;
};
