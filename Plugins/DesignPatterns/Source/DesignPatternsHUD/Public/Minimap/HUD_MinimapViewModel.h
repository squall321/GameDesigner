// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MVVM/DPViewModelBase.h"
#include "GameplayTagContainer.h"
#include "FieldNotification/IClassDescriptor.h"
#include "Minimap/HUD_MinimapProjectionStrategy.h"
#include "HUD_MinimapViewModel.generated.h"

class UHUD_MarkerRegistrySubsystem;
class UTexture2D;

/**
 * One projected marker as the view consumes it: where to draw it (normalized minimap space), which icon
 * to draw (resolved from the marker tag), and the bearing/off-map flag for edge indicators. The view
 * binds to the Markers field and re-reads this flat array — it never touches gameplay or the registry.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_MinimapMarkerView
{
	GENERATED_BODY()

	/** The marker kind tag (icon key + designer-facing identity). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	FGameplayTag MarkerTag;

	/** Soft icon for this marker, resolved from MarkerTag via the tag->icon table (may be unloaded). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	TSoftObjectPtr<UTexture2D> Icon;

	/** Normalized minimap-local position, components in [-1, 1]; (0,0) is map center. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	FVector2D NormalizedPosition = FVector2D::ZeroVector;

	/** Heading (degrees, 0 = map-up) from center to the marker — for edge arrows. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	float BearingDegrees = 0.f;

	/** True if the marker is inside the map radius; false = clamped to the edge (off-map). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|HUD|Minimap")
	bool bWithinRange = true;
};

/** A single tag->icon mapping row for the minimap's icon table. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSHUD_API FHUD_MarkerIconRow
{
	GENERATED_BODY()

	/** Marker kind tag this icon applies to (matched exactly or as a parent of the marker's tag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|Minimap")
	FGameplayTag MarkerTag;

	/** Icon drawn for markers of this kind. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|HUD|Minimap")
	TSoftObjectPtr<UTexture2D> Icon;
};

/**
 * ViewModel that projects the live set of registered IHUD_Trackable markers into a flat, view-ready array.
 *
 * It owns an instanced UHUD_MinimapProjectionStrategy (top-down, rotated by default) and an icon table
 * (tag -> soft texture). On each Refresh it snapshots the world UHUD_MarkerRegistrySubsystem, filters by
 * IsVisibleOnMap, projects each marker through the strategy relative to the configured view frame, resolves
 * its icon by tag, and writes the result to the FieldNotify Markers property — any bound UDP_ViewBase then
 * re-reads it. The ViewModel holds NO gameplay pointers: it speaks only to the registry (resolved by stable
 * service tag) and the IHUD_Trackable seam, exactly per the MVVM contract.
 *
 * Refresh is push-driven (call SetViewFrame + Refresh from the owning HUD widget's tick, or hook the
 * registry's OnMarkerSetChanged for set changes). The ViewModel is local/cosmetic and never replicated.
 */
UCLASS(BlueprintType, meta = (DisplayName = "HUD Minimap ViewModel"))
class DESIGNPATTERNSHUD_API UHUD_MinimapViewModel : public UDP_ViewModelBase
{
	GENERATED_BODY()

public:
	UHUD_MinimapViewModel();

	/** Stable, ordered ids for this ViewModel's observable fields. */
	enum class EField : int32
	{
		/** The projected, view-ready marker array. */
		Markers = 0,
		Num
	};

	//~ Begin INotifyFieldValueChanged
	virtual const UE::FieldNotification::IClassDescriptor& GetFieldNotificationDescriptor() const override;
	//~ End INotifyFieldValueChanged

	/** Resolve the FFieldId for one of this ViewModel's fields. */
	static UE::FieldNotification::FFieldId GetFieldId(EField Field);

	/**
	 * Update the view frame (player/camera center + yaw) the next Refresh projects relative to. Does not
	 * itself reproject; call Refresh after (typically both per-frame from the owning widget).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Minimap")
	void SetViewFrame(const FVector& InViewOriginWorld, float InViewYawDegrees);

	/**
	 * Re-snapshot the marker registry, project + resolve icons, and broadcast the Markers field change if
	 * the projected set actually differs. Resolves the registry by service tag from the supplied
	 * world-context object (the owning widget), so the ViewModel needs no world pointer of its own.
	 *
	 * @param WorldContextObject  Any object with a world (e.g. the owning view widget). No-op if null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Minimap")
	void Refresh(UObject* WorldContextObject);

	/**
	 * The projected, view-ready markers (observable field EField::Markers). The view binds to this
	 * ViewModel's field-changed multicast and re-reads this getter when Markers broadcasts. Returned by
	 * value for Blueprint safety (UHT does not permit a UFUNCTION to return a container by const ref).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Minimap")
	TArray<FHUD_MinimapMarkerView> GetMarkers() const { return Markers; }

	/** Replace the icon table (tag -> soft texture) used to resolve marker icons. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Minimap")
	void SetIconTable(const TArray<FHUD_MarkerIconRow>& InRows);

	/** Replace the projection strategy (e.g. swap to a fixed-north strategy). Null is ignored. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Minimap")
	void SetProjectionStrategy(UHUD_MinimapProjectionStrategy* InStrategy);

	/** The current projection strategy (never null after construction). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Minimap")
	UHUD_MinimapProjectionStrategy* GetProjectionStrategy() const { return ProjectionStrategy; }

private:
	/** Resolve the best icon for a marker tag from IconTable (exact, else nearest parent), or null. */
	TSoftObjectPtr<UTexture2D> ResolveIconForTag(const FGameplayTag& MarkerTag) const;

	/** The instanced projection strategy. Owning UPROPERTY so it is GC-kept and editable inline. */
	UPROPERTY(EditAnywhere, Instanced, Category = "DesignPatterns|HUD|Minimap",
		meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UHUD_MinimapProjectionStrategy> ProjectionStrategy = nullptr;

	/** Tag -> icon table used to resolve each marker's icon. Designer-authored / pushed at runtime. */
	UPROPERTY(EditAnywhere, Category = "DesignPatterns|HUD|Minimap",
		meta = (AllowPrivateAccess = "true"))
	TArray<FHUD_MarkerIconRow> IconTable;

	/** The projected, view-ready marker set (the observable field, EField::Markers). */
	UPROPERTY(Transient, Category = "DesignPatterns|HUD|Minimap", meta = (AllowPrivateAccess = "true"))
	TArray<FHUD_MinimapMarkerView> Markers;

	/** Current view frame origin used by the next Refresh. */
	FVector ViewOriginWorld = FVector::ZeroVector;

	/** Current view frame yaw (degrees) used by the next Refresh. */
	float ViewYawDegrees = 0.f;
};
