// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/IClassDescriptor.h"
#include "HUD_DamageNumberViewModel.generated.h"

/**
 * One floating combat-text item as the view consumes it: where to draw it (screen-space, already projected
 * from the world impact point), the text to draw, its style color/scale/prefix (resolved from the style
 * asset), and a normalized [0,1] lifetime alpha the view uses to drive rise + fade. The view binds the
 * Numbers field and re-reads this flat array — it never touches gameplay, the bus, or the world.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_FloatingTextView
{
	GENERATED_BODY()

	/** Stable instance id for the active item (so a view can animate per-item without index churn). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber")
	int64 InstanceId = 0;

	/** The composed display text (prefix + amount), already formatted. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber")
	FText Text;

	/** Spawn anchor in viewport pixels (the projected impact point + jitter). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber")
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/** Resolved text color for this item's classification. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber")
	FLinearColor Color = FLinearColor::White;

	/** Final font scale (base scale * per-classification multiplier). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber")
	float Scale = 1.f;

	/** Pixels this item rises over its full lifetime (the view interpolates by LifetimeAlpha). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber")
	float RisePixels = 0.f;

	/** Normalized lifetime in [0,1] (0 = just spawned, 1 = about to recycle). Drives rise + fade in the view. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber")
	float LifetimeAlpha = 0.f;

	/** The classification tag (crit/heal/weakpoint/normal) in case the view wants extra per-kind treatment. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|DamageNumber")
	FGameplayTag ClassificationTag;
};

/**
 * ViewModel projecting the active floating-text pool into a flat, view-ready array.
 *
 * Mirrors UHUD_NotificationViewModel / UHUD_MinimapViewModel exactly: a hand-rolled EField enum + GetFieldId
 * + GetFieldNotificationDescriptor + a private BroadcastField, built on the engine FieldNotification system
 * via UDP_ViewModelBase (NOT the optional MVVM plugin). It holds NO world/gameplay pointers — the
 * subsystem owns it and pushes the current set each tick.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Damage Number ViewModel"))
class DESIGNPATTERNSHUD_API UHUD_DamageNumberViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this ViewModel's observable fields. */
	enum class EField : int32
	{
		/** The active floating-text array. */
		Numbers = 0,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this ViewModel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/** Replace the active floating-text set (called by the subsystem each tick); broadcasts on change. */
	void SetNumbers(const TArray<FHUD_FloatingTextView>& InNumbers);

	/** The active floating-text items, in spawn order (copied for BP safety). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|DamageNumber")
	TArray<FHUD_FloatingTextView> GetNumbers() const { return Numbers; }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Backing storage for the active floating-text items (the observable field). */
	UPROPERTY(Transient)
	TArray<FHUD_FloatingTextView> Numbers;
};
