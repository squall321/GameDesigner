// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Pool/DPWidgetPoolTypes.h"
#include "DPWidgetPoolSubsystem.generated.h"

class UUserWidget;
class APlayerController;
class UDP_ViewBase;
class UDP_ViewModelBase;

/**
 * GameInstance-scoped recycler for UUserWidget / UDP_ViewBase instances.
 *
 * WHY A SEPARATE POOL (not the core UDP_ObjectPoolSubsystem):
 *   The core object pool is WORLD-scoped and oriented around AActors (its reset contract zeroes
 *   velocity, restores collision/tick, places a transform). Widgets are UObjects, not actors, and
 *   their natural owner is a UGameInstance-lived player rather than the world. Routing widgets
 *   through the actor pool would be wrong on both counts, so this subsystem applies the SAME
 *   object-pool CONCEPT specialised for widgets and deliberately does NOT hold or cache the
 *   world-scoped pool across travel.
 *
 * KEYING: buckets are keyed by (WidgetClass, OwningPlayer) so split-screen players recycle their
 *   own widgets. Buckets whose OwningPlayer has been destroyed are pruned on the next touch.
 *
 * RESET CONTRACT (structural only): on RELEASE the subsystem removes the widget from its parent/
 * viewport, clears its ViewModel (if it is a UDP_ViewBase), and resets render transform + render
 * opacity to identity. On ACQUIRE it reverses the visual reset and, for a view, assigns the
 * requested ViewModel. Latent widget state (running animations, bound delegates, anim-driver
 * handles, tooltip registrations) is the widget's own responsibility via the optional
 * IDP_WidgetPoolable hooks, which this subsystem invokes if implemented.
 *
 * Pairs with UDP_WidgetAnimDriver (fade-in on acquire / fade-out then auto-release) and the
 * tooltip/notice presenters which churn many short-lived widgets.
 */
UCLASS()
class DESIGNPATTERNSUI_API UDP_WidgetPoolSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/**
	 * Acquire a pooled widget of WidgetClass for OwningPlayer. Reuses an idle instance if one is
	 * available, otherwise constructs a new one with CreateWidget. The widget is NOT added to the
	 * viewport — the caller chooses where to place it (AddToViewport / a panel slot). Applies the
	 * structural reset reversal and fires IDP_WidgetPoolable::OnAcquiredFromWidgetPool.
	 *
	 * @param WidgetClass   The widget class to acquire (must be non-abstract).
	 * @param OwningPlayer  The player that will own the widget; null falls back to the first local PC.
	 * @return A ready widget, or null on failure (bad class / no player / construction failure).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Pool", meta = (DeterminesOutputType = "WidgetClass"))
	UUserWidget* AcquireWidget(TSubclassOf<UUserWidget> WidgetClass, APlayerController* OwningPlayer = nullptr);

	/**
	 * Acquire a pooled UDP_ViewBase and assign ViewModel to it in one call (the common MVVM case).
	 * Returns null if ViewClass does not resolve to a UDP_ViewBase. ViewModel may be null.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Pool", meta = (DeterminesOutputType = "ViewClass"))
	UDP_ViewBase* AcquireView(TSubclassOf<UDP_ViewBase> ViewClass, UDP_ViewModelBase* ViewModel = nullptr,
		APlayerController* OwningPlayer = nullptr);

	/**
	 * Return a widget to its pool: removes it from parent/viewport, clears its ViewModel, resets
	 * its render transform/opacity, fires IDP_WidgetPoolable::OnReturnedToWidgetPool and parks it
	 * idle for reuse. Double-release is a no-op. The widget must not be used after this returns.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Pool")
	void ReleaseWidget(UUserWidget* Widget);

	/**
	 * Pre-create up to Count idle instances of WidgetClass for OwningPlayer so the first burst of
	 * acquires does not allocate. Constructed instances are reset and parked idle immediately.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Pool")
	void WarmPool(TSubclassOf<UUserWidget> WidgetClass, int32 Count, APlayerController* OwningPlayer = nullptr);

	/**
	 * Drain every idle instance of WidgetClass across ALL owning players (reclaimable ones only —
	 * IDP_WidgetPoolable::CanWidgetBeReclaimed may veto). Live instances are left untouched.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|UI|Pool")
	void DrainPool(TSubclassOf<UUserWidget> WidgetClass);

	/** Number of idle (available) instances of WidgetClass across all owning players. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Pool")
	int32 GetIdleCount(TSubclassOf<UUserWidget> WidgetClass) const;

	/** Number of currently checked-out (live) instances of WidgetClass across all owning players. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|UI|Pool")
	int32 GetLiveCount(TSubclassOf<UUserWidget> WidgetClass) const;

private:
	/** Resolve the first local player controller of this game instance, or null. */
	APlayerController* GetFirstLocalPlayerController() const;

	/** Build a pool key from a class + player (player null -> first local PC). */
	FDP_WidgetPoolKey MakeKey(TSubclassOf<UUserWidget> WidgetClass, APlayerController* OwningPlayer) const;

	/** Construct a brand-new widget for Key (CreateWidget with Key.OwningPlayer). */
	UUserWidget* CreateNewWidget(const FDP_WidgetPoolKey& Key) const;

	/** Apply the structural release reset (remove from parent, clear VM, reset transform/opacity). */
	static void ApplyReleaseReset(UUserWidget* Widget);

	/** Apply the structural acquire reset reversal (identity transform/opacity, full opacity). */
	static void ApplyAcquireReset(UUserWidget* Widget);

	/** Fire IDP_WidgetPoolable::OnAcquiredFromWidgetPool if implemented. */
	static void NotifyAcquired(UUserWidget* Widget);

	/** Fire IDP_WidgetPoolable::OnReturnedToWidgetPool if implemented. */
	static void NotifyReturned(UUserWidget* Widget);

	/** Query IDP_WidgetPoolable::CanWidgetBeReclaimed (true when the interface is unoverridden). */
	static bool CanReclaim(const UUserWidget* Widget);

	/** Drop buckets whose OwningPlayer has been GC'd, releasing their (now ownerless) idle widgets. */
	void PruneDeadOwners();

	/** Per-(class,player) pool storage. UPROPERTY so owned widgets stay reachable by GC. */
	UPROPERTY()
	TMap<FDP_WidgetPoolKey, FDP_WidgetPoolBucket> Buckets;
};
