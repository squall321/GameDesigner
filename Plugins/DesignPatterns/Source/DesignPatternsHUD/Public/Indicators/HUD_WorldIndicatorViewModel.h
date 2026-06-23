// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Minimap/HUD_MinimapViewModel.h" // FHUD_MarkerIconRow
#include "HUD_WorldIndicatorViewModel.generated.h"

class UTexture2D;

/**
 * One world indicator as the view consumes it: an on-screen marker (when the target projects inside the
 * viewport) OR an off-screen edge arrow (clamped to the viewport edge, pointing at the target). The view
 * binds the Indicators field and re-reads this flat array — it never touches gameplay or the registry.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_WorldIndicatorView
{
	GENERATED_BODY()

	/** Marker kind tag (icon key + identity). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator")
	FGameplayTag MarkerTag;

	/** Soft icon resolved from MarkerTag via the config icon table (may be unloaded). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Viewport-pixel position: the projected target when on-screen, else the clamped edge position. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator")
	FVector2D ScreenPosition = FVector2D::ZeroVector;

	/** True if the target projected inside the viewport (draw a marker); false = off-screen (draw an arrow). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator")
	bool bOnScreen = true;

	/** Arrow heading in degrees (0 = up), valid when !bOnScreen, pointing from the edge toward the target. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator")
	float ArrowAngleDegrees = 0.f;

	/** Final opacity in [0,1] after distance fade + occlusion dimming. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator")
	float Opacity = 1.f;

	/** Distance (uu) from the viewer to the target (for an optional distance readout). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator")
	float DistanceUU = 0.f;

	/** Number of targets merged into this indicator by clustering (1 = not clustered). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Indicator")
	int32 ClusterCount = 1;
};

/**
 * ViewModel projecting the live trackable set into on/off-screen world indicators.
 *
 * Mirrors UHUD_MinimapViewModel exactly (hand-rolled EField + GetFieldId + descriptor + private
 * BroadcastField, built on UDP_ViewModelBase). It holds NO gameplay pointers — the indicator subsystem
 * owns it, resolves the registry, projects/clusters, and pushes the result each refresh.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD World Indicator ViewModel"))
class DESIGNPATTERNSHUD_API UHUD_WorldIndicatorViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	/** Stable, ordered ids for this ViewModel's observable fields. */
	enum class EField : int32
	{
		/** The projected on/off-screen indicator array. */
		Indicators = 0,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this ViewModel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/** Replace the projected indicator set (called by the subsystem each refresh); broadcasts on change. */
	void SetIndicators(const TArray<FHUD_WorldIndicatorView>& InIndicators);

	/** Replace the tag->icon table used to resolve indicator icons. */
	void SetIconTable(const TArray<FHUD_MarkerIconRow>& InRows);

	/** Resolve the best icon for a marker tag from the icon table (exact, else nearest parent), or null. */
	TSoftObjectPtr<UTexture2D> ResolveIconForTag(const FGameplayTag& MarkerTag) const;

	/** The projected indicators (copied for BP safety). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|HUD|Indicator")
	TArray<FHUD_WorldIndicatorView> GetIndicators() const { return Indicators; }

private:
	/** Broadcast a field change by enum id. */
	void BroadcastField(EField Field);

	/** Backing storage for the projected indicators (the observable field). */
	UPROPERTY(Transient)
	TArray<FHUD_WorldIndicatorView> Indicators;

	/** Tag -> icon table used to resolve each indicator's icon. */
	UPROPERTY(Transient)
	TArray<FHUD_MarkerIconRow> IconTable;
};
