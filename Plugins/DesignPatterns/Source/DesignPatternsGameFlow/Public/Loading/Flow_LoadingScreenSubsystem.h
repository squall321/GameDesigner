// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Engine/StreamableManager.h"
#include "GameplayTagContainer.h"
#include "Flow_LoadingScreenSubsystem.generated.h"

class UFlow_LoadingViewModel;
class UDP_MessageBusSubsystem;
class UFlow_StreamingLoadCoordinator;

/** Coarse loading lifecycle the progress model surfaces to the UI. */
UENUM(BlueprintType)
enum class EFlow_LoadingState : uint8
{
	/** No load in progress; loading screen hidden. */
	Idle,
	/** Async-loading the target asset(s) before travel (we drive a real fraction here). */
	Preloading,
	/** Engine map flush is happening (PreLoadMap fired; indeterminate fraction). */
	MapLoading,
	/** PostLoadMap fired; finishing up (about to hide). */
	Finishing
};

/**
 * GameInstance-scoped loading-screen orchestrator. WRAPS the engine's loading machinery — it does NOT
 * reinvent map loading:
 *  - subscribes to FCoreUObjectDelegates::PreLoadMap / PostLoadMapWithWorld so a loading screen is
 *    shown for EVERY map change (including ones started elsewhere, e.g. by the flow subsystem's travel);
 *  - registers a movie loading screen via GetMoviePlayer()->SetupLoadingScreen so the engine blocks on
 *    it during the synchronous map flush (configurable auto-complete / minimum display time);
 *  - optionally async-loads a target asset set through a shared FStreamableManager BEFORE travel, with a
 *    real progress fraction, then hands off to the engine map load (indeterminate fraction);
 *  - pushes a normalized progress + label into a UFlow_LoadingViewModel and broadcasts it on
 *    DP.Bus.Flow.LoadingProgress so any HUD can mirror it.
 *
 * It holds NO replicated state (a subsystem never does); the loading screen is a local presentation of
 * an already-decided travel.
 */
UCLASS()
class DESIGNPATTERNSGAMEFLOW_API UFlow_LoadingScreenSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Begin a preload of TargetAssets (soft paths), updating a real progress fraction, then leave the
	 * screen up for the subsequent engine map load. Pass an empty array to show the loading screen with
	 * an indeterminate fraction (pure map load). StatusLabel is surfaced to the loading UI.
	 *
	 * This does NOT itself travel — the flow subsystem (or caller) issues the OpenLevel/ClientTravel; the
	 * engine PreLoadMap delegate then drives the rest. Begin/preload here just front-loads asset streaming
	 * and primes the progress model.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flow|Loading")
	void BeginPreload(const TArray<FSoftObjectPath>& TargetAssets, FText StatusLabel, const FString& TargetMapName);

	/** Cancel any in-flight preload and reset the progress model to Idle. */
	UFUNCTION(BlueprintCallable, Category = "Flow|Loading")
	void CancelPreload();

	/** Current coarse loading state. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Loading")
	EFlow_LoadingState GetLoadingState() const { return LoadingState; }

	/** Current normalized progress in [0,1], or -1 when indeterminate. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Loading")
	float GetProgress() const { return CurrentProgress; }

	/** The loading ViewModel the loading UI binds to (created lazily; never null after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Loading")
	UFlow_LoadingViewModel* GetViewModel() const { return ViewModel; }

	/**
	 * Begin aggregating a streaming-sublevel load for the given content categories into the loading bar
	 * (driven through the shared ISeam_StreamingControl seam). Additive convenience over the owned
	 * coordinator; no-op (preload-only) when no streaming adapter is registered or Categories is empty.
	 */
	UFUNCTION(BlueprintCallable, Category = "Flow|Loading")
	void BeginStreamingLoad(const FGameplayTagContainer& Categories, FText Label);

	/** Release any active streaming aggregate (e.g. on cancel / phase change). Additive. */
	UFUNCTION(BlueprintCallable, Category = "Flow|Loading")
	void EndStreamingLoad();

	/** The owned streaming-load coordinator (never null after Initialize). Additive. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flow|Loading")
	UFlow_StreamingLoadCoordinator* GetStreamingCoordinator() const { return StreamingCoordinator; }

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	// --- Engine delegate handlers ---

	/** FCoreUObjectDelegates::PreLoadMap: register the movie loading screen and flip to MapLoading. */
	void HandlePreLoadMap(const FString& MapName);

	/** FCoreUObjectDelegates::PostLoadMapWithWorld: flip to Finishing then Idle and hide the screen. */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	// --- Internal model updates ---

	/** Set state + progress, push into the ViewModel and broadcast DP.Bus.Flow.LoadingProgress. */
	void UpdateProgress(EFlow_LoadingState NewState, float NewProgress, const FText& Label);

	/** Tick callback driving the preload fraction from the streamable handle until it completes. */
	bool TickPreload(float DeltaTime);

	/** Configure + show the engine movie loading screen from the Flow settings (auto-complete, min time). */
	void SetupEngineLoadingScreen(const FText& Label);

	/** Resolve the message bus for this GameInstance, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	// --- State ---

	/** The loading ViewModel (owning ref; created in Initialize). */
	UPROPERTY(Transient)
	TObjectPtr<UFlow_LoadingViewModel> ViewModel = nullptr;

	/** Current coarse loading lifecycle state. */
	EFlow_LoadingState LoadingState = EFlow_LoadingState::Idle;

	/** Current normalized progress, or -1 for indeterminate. */
	float CurrentProgress = -1.f;

	/** The current status label (surfaced to the UI). */
	FText CurrentLabel;

	/** The map currently being loaded (for diagnostics / payload). */
	FString CurrentMapName;

	/** Streamable manager owning the preload async load. */
	FStreamableManager StreamableManager;

	/** In-flight preload handle (null when no preload is active). */
	TSharedPtr<FStreamableHandle> PreloadHandle;

	/** Ticker handle driving the preload fraction. */
	FTSTicker::FDelegateHandle PreloadTickHandle;

	/** Engine delegate handles, removed on Deinitialize. */
	FDelegateHandle PreLoadMapHandle;
	FDelegateHandle PostLoadMapHandle;

	/** Owned streaming-load coordinator (aggregates sublevel streaming into the bar). Additive. */
	UPROPERTY(Transient)
	TObjectPtr<UFlow_StreamingLoadCoordinator> StreamingCoordinator = nullptr;
};
