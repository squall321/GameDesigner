// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Mod_HotReloadSubsystem.generated.h"

#if WITH_EDITOR
struct FFileChangeData;
#endif

/**
 * EDITOR/DEV-ONLY hot-reload subsystem for content packs.
 *
 * The ENTIRE class is gated to editor builds: ShouldCreateSubsystem returns false outside the editor, and
 * all watcher machinery is compiled under WITH_EDITOR, so nothing of it exists in a shipping build. When
 * enabled (UMod_DeveloperSettings::bEnableEditorHotReload) it wraps the engine IDirectoryWatcher over the
 * configured DiscoveryDirectories, debounces a burst of file changes into one re-scan, and then calls
 * ONLY the manager's PUBLIC API to refresh: UnmountPack -> DiscoverPacks -> MountPack (re-mounts still go
 * through validate-before-activate; mod code is never auto-executed).
 *
 * It reaches into no manager private state. GameInstance-scoped; non-replicated, non-saved. Every watcher
 * handle and the debounce ticker are removed in Deinitialize.
 */
UCLASS()
class DESIGNPATTERNSMODCONTENT_API UMod_HotReloadSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Begin watching the configured discovery directories (no-op outside editor / when disabled). */
	UFUNCTION(BlueprintCallable, Category = "ModContent|HotReload")
	void StartWatching();

	/** Stop watching and release every directory-watch handle. Safe to call when not watching. */
	UFUNCTION(BlueprintCallable, Category = "ModContent|HotReload")
	void StopWatching();

	/** True while at least one directory is being watched. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|HotReload")
	bool IsWatching() const;

	/**
	 * Explicitly hot-reload one pack: unmount it (if mounted), re-discover, then re-mount it. Public so an
	 * editor command can trigger a single-pack reload without a file event. No-op outside editor.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|HotReload")
	bool HotReloadPack(FGameplayTag PackId);

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

#if WITH_EDITOR
private:
	/** Directory-watcher callback: queue a debounced re-scan (does not act immediately to coalesce bursts). */
	void OnDirectoryChanged(const TArray<FFileChangeData>& Changes);

	/** Debounce ticker body: when the quiet window elapses, run the manager refresh. Returns false to stop. */
	bool TickDebounce(float DeltaTime);

	/** Run the public manager refresh: re-discover, then re-mount everything that was mounted before. */
	void PerformHotReload();

	/** Editor implementation behind the public HotReloadPack; writes the success result into bOutResult. */
	void HotReloadPack_Internal(FGameplayTag PackId, bool& bOutResult);

	/** Resolve the absolute discovery directories from settings (project-relative resolved). */
	void GatherWatchedDirectories(TArray<FString>& OutAbsoluteDirs) const;

	/** Handles into IDirectoryWatcher, one per watched directory, released on stop/deinit. */
	TMap<FString, FDelegateHandle> WatchHandles;

	/** Debounce ticker handle (FTSTicker). Cleared on fire/stop/deinit. */
	FTSTicker::FDelegateHandle DebounceTickerHandle;

	/** Seconds remaining in the current debounce window; a change resets it to the configured value. */
	double DebounceRemainingSeconds = 0.0;

	/** True when a change has been seen and a re-scan is pending. */
	bool bReloadPending = false;
#endif // WITH_EDITOR
};
