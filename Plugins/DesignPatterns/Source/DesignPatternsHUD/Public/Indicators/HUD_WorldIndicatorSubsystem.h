// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/LocalPlayerSubsystem.h"
#include "Containers/Ticker.h"
#include "HUD_WorldIndicatorSubsystem.generated.h"

class UHUD_WorldIndicatorViewModel;
class UHUD_WorldIndicatorConfigDataAsset;
class UHUD_MarkerRegistrySubsystem;
class APlayerController;
struct FHUD_WorldIndicatorView;

/**
 * Per-local-player off/on-screen world indicator presenter (threat arrows, objective markers, points of
 * interest).
 *
 * READS the world-scoped UHUD_MarkerRegistrySubsystem (resolved by HUDTags::Service_MarkerRegistry, weak)
 * via GetLiveTrackables, projecting each IHUD_Trackable through the owning PlayerController. For each visible
 * trackable it either emits an on-screen marker (projected inside the viewport) or an off-screen edge arrow
 * clamped to the viewport edge pointing at the target. It applies distance fade, a single capped occlusion
 * line trace per target, and pixel-radius clustering of nearby on-screen markers. Each refresh it pushes the
 * result into UHUD_WorldIndicatorViewModel; the bound UDP_ViewBase renders it.
 *
 * Local-player-scoped (NOT a world subsystem) so projection is always per-viewer-correct. Purely
 * local/cosmetic: it never replicates and never mutates gameplay; the registry it reads is fed by
 * already-replicated actors present on every client.
 */
UCLASS()
class DESIGNPATTERNSHUD_API UHUD_WorldIndicatorSubsystem : public ULocalPlayerSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Replace the active config/tuning asset and re-push its icon table into the ViewModel. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Indicator")
	void SetConfig(UHUD_WorldIndicatorConfigDataAsset* InConfig);

	/** The ViewModel the indicator UI binds to (never null after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|HUD|Indicator")
	UHUD_WorldIndicatorViewModel* GetViewModel() const { return ViewModel; }

	/** Force a single immediate refresh (otherwise driven by the FTSTicker). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|HUD|Indicator")
	void RefreshForViewer();

	/** Append a one-line debug summary. */
	void DumpTo(TArray<FString>& OutLines) const;

private:
	/** A trackable projected to screen, pre-cluster. */
	struct FProjected
	{
		FGameplayTag MarkerTag;
		FVector WorldLocation = FVector::ZeroVector;
		FVector2D ScreenPosition = FVector2D::ZeroVector;
		bool bOnScreen = true;
		float ArrowAngleDegrees = 0.f;
		float DistanceUU = 0.f;
		float Opacity = 1.f;
	};

	/** FTSTicker callback: gate on a valid PC/viewport, then RefreshForViewer. */
	bool TickIndicators(float DeltaTime);

	/** Resolve the world marker registry (by service tag, else world subsystem); null in editor/preview. */
	UHUD_MarkerRegistrySubsystem* ResolveRegistry() const;

	/** The owning local player's current PlayerController (null if not yet possessed). */
	APlayerController* GetOwningPlayerController() const;

	/** Project + classify all live trackables into Out (on-screen / edge-clamped + fade + occlusion). */
	void ProjectAndClassify(TArray<FProjected>& Out) const;

	/** Merge on-screen entries within the config cluster radius; off-screen arrows are never clustered. */
	void ClusterScreenIndicators(TArray<FProjected>& InOut, TArray<FHUD_WorldIndicatorView>& OutViews) const;

	/** Single line trace from target to viewer; true when blocked (occluded). */
	bool IsOccluded(const FVector& Target, APlayerController* PC) const;

	/** The pure-projection ViewModel (owned, GC-kept). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_WorldIndicatorViewModel> ViewModel = nullptr;

	/** The active config/tuning asset (owned ref so it is GC-kept while bound). */
	UPROPERTY(Transient)
	TObjectPtr<UHUD_WorldIndicatorConfigDataAsset> Config = nullptr;

	/** FTSTicker handle driving TickIndicators; removed in Deinitialize. */
	FTSTicker::FDelegateHandle TickerHandle;
};
