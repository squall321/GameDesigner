// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "FieldNotification/IClassDescriptor.h"
#include "HUD_ScreenStateViewModel.generated.h"

/**
 * One active hit-direction indicator as the view consumes it: the screen-space bearing (degrees, 0 = up,
 * clockwise) from which damage came, plus a [0,1] alpha for fade. The view rotates a directional indicator
 * to AngleDegrees and fades it by Alpha.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_HitDirectionView
{
	GENERATED_BODY()

	/** Bearing in degrees (0 = up / forward, clockwise positive) from which the damage came. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState")
	float AngleDegrees = 0.f;

	/** Fade alpha in [0,1] (1 = fresh, 0 = about to clear). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|ScreenState")
	float Alpha = 1.f;
};

/**
 * ViewModel projecting full-screen state-effect values: low-health vignette intensity, the active
 * hit-direction indicators, and the damage-flash alpha. The bound UDP_ViewBase maps these to material /
 * MPC parameters — the subsystem NEVER touches materials.
 *
 * Mirrors the shipped VM IClassDescriptor pattern (hand-rolled EField + GetFieldId + descriptor + private
 * BroadcastField on UDP_ViewModelBase). Pure projection — no world/gameplay pointers.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Screen State ViewModel"))
class DESIGNPATTERNSHUD_API UHUD_ScreenStateViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this ViewModel's observable fields. */
	enum class EField : int32
	{
		VignetteIntensity = 0,
		HitDirections,
		DamageFlashAlpha,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this ViewModel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/** Set the low-health vignette intensity in [0,1]; broadcasts on change. */
	void SetVignetteIntensity(float Intensity);

	/** Replace the active hit-direction indicator set; broadcasts (always, since alphas tick each frame). */
	void SetHitDirections(const TArray<FHUD_HitDirectionView>& InDirections);

	/** Set the full-screen damage-flash alpha in [0,1]; broadcasts on change. */
	void SetDamageFlashAlpha(float Alpha);

	// --- Observable getters ---

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|ScreenState")
	float GetVignetteIntensity() const { return VignetteIntensity; }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|ScreenState")
	TArray<FHUD_HitDirectionView> GetHitDirections() const { return HitDirections; }

	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|ScreenState")
	float GetDamageFlashAlpha() const { return DamageFlashAlpha; }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	UPROPERTY(Transient)
	float VignetteIntensity = 0.f;

	UPROPERTY(Transient)
	TArray<FHUD_HitDirectionView> HitDirections;

	UPROPERTY(Transient)
	float DamageFlashAlpha = 0.f;
};
