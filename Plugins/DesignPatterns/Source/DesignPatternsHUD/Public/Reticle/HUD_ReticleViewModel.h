// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/IClassDescriptor.h"
#include "HUD_ReticleViewModel.generated.h"

/**
 * ViewModel projecting the crosshair/reticle state: spread (degrees), hit-confirm alpha, the centre
 * target-type tag (friendly/hostile/neutral), and overall visibility. The bound UDP_ViewBase maps these to
 * crosshair gap, hit-marker opacity, and team color.
 *
 * Mirrors the shipped VM IClassDescriptor pattern (hand-rolled EField + GetFieldId + descriptor + private
 * BroadcastField on UDP_ViewModelBase). Pure projection — no world/gameplay pointers.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Reticle ViewModel"))
class DESIGNPATTERNSHUD_API UHUD_ReticleViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this ViewModel's observable fields. */
	enum class EField : int32
	{
		SpreadDegrees = 0,
		HitConfirmAlpha,
		TargetTypeTag,
		bVisible,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this ViewModel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/** Set the crosshair spread in degrees (drives crosshair gap); broadcasts on change. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Reticle")
	void SetSpread(float Degrees);

	/** Set the hit-confirm alpha directly in [0,1]; broadcasts on change. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Reticle")
	void SetHitConfirmAlpha(float Alpha);

	/** Set the centre target-type tag (friendly/hostile/neutral); broadcasts on change. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Reticle")
	void SetTargetTypeTag(FGameplayTag Tag);

	/** Set reticle visibility; broadcasts on change. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Reticle")
	void SetVisible(bool bInVisible);

	// --- Observable getters ---

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|Reticle")
	float GetSpreadDegrees() const { return SpreadDegrees; }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|Reticle")
	float GetHitConfirmAlpha() const { return HitConfirmAlpha; }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|Reticle")
	FGameplayTag GetTargetTypeTag() const { return TargetTypeTag; }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|Reticle")
	bool IsVisible() const { return bVisible; }

private:
	UPROPERTY(Transient)
	float SpreadDegrees = 0.f;

	UPROPERTY(Transient)
	float HitConfirmAlpha = 0.f;

	UPROPERTY(Transient)
	FGameplayTag TargetTypeTag;

	UPROPERTY(Transient)
	bool bVisible = true;
};
