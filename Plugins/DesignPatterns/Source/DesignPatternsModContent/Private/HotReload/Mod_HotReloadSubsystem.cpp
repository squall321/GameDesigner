// Copyright DesignPatterns plugin. All Rights Reserved.

#include "HotReload/Mod_HotReloadSubsystem.h"
#include "Manager/Mod_ContentManagerSubsystem.h"
#include "Settings/Mod_DeveloperSettings.h"
#include "DesignPatternsModContentModule.h"

#include "Core/DPLog.h"
#include "Engine/GameInstance.h"

#if WITH_EDITOR
#include "DirectoryWatcherModule.h"
#include "IDirectoryWatcher.h"
#include "Modules/ModuleManager.h"
#include "Containers/Ticker.h"
#include "Misc/Paths.h"
#endif

// =====================================================================================================
// Creation gate (defined for all configs)
// =====================================================================================================

bool UMod_HotReloadSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
#if WITH_EDITOR
	const UMod_DeveloperSettings* Settings = UMod_DeveloperSettings::Get();
	return Settings && Settings->bEnableEditorHotReload;
#else
	return false;
#endif
}

// =====================================================================================================
// Lifecycle (defined for all configs; bodies branch on WITH_EDITOR)
// =====================================================================================================

void UMod_HotReloadSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_EDITOR
	StartWatching();
#endif
}

void UMod_HotReloadSubsystem::Deinitialize()
{
#if WITH_EDITOR
	StopWatching();
	if (DebounceTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(DebounceTickerHandle);
		DebounceTickerHandle.Reset();
	}
#endif
	Super::Deinitialize();
}

bool UMod_HotReloadSubsystem::IsWatching() const
{
#if WITH_EDITOR
	return WatchHandles.Num() > 0;
#else
	return false;
#endif
}

FString UMod_HotReloadSubsystem::GetDPDebugString_Implementation() const
{
#if WITH_EDITOR
	return FString::Printf(TEXT("HotReload(editor): watching=%d dir(s), pending=%s"),
		WatchHandles.Num(), bReloadPending ? TEXT("yes") : TEXT("no"));
#else
	return TEXT("HotReload: disabled (non-editor build)");
#endif
}

// =====================================================================================================
// Editor-only watcher machinery
// =====================================================================================================

#if WITH_EDITOR

void UMod_HotReloadSubsystem::GatherWatchedDirectories(TArray<FString>& OutAbsoluteDirs) const
{
	OutAbsoluteDirs.Reset();
	const UMod_DeveloperSettings* Settings = UMod_DeveloperSettings::Get();
	if (!Settings)
	{
		return;
	}
	for (const FDirectoryPath& Dir : Settings->DiscoveryDirectories)
	{
		if (Dir.Path.IsEmpty())
		{
			continue;
		}
		const FString Abs = FPaths::ConvertRelativePathToFull(
			FPaths::IsRelative(Dir.Path) ? (FPaths::ProjectDir() / Dir.Path) : Dir.Path);
		OutAbsoluteDirs.AddUnique(Abs);
	}
}

void UMod_HotReloadSubsystem::StartWatching()
{
	if (WatchHandles.Num() > 0)
	{
		return; // already watching
	}

	FDirectoryWatcherModule& Module =
		FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
	IDirectoryWatcher* Watcher = Module.Get();
	if (!Watcher)
	{
		UE_LOG(LogDP, Warning, TEXT("ModContent: DirectoryWatcher unavailable; hot-reload disabled."));
		return;
	}

	TArray<FString> Dirs;
	GatherWatchedDirectories(Dirs);

	for (const FString& Dir : Dirs)
	{
		if (!FPaths::DirectoryExists(Dir))
		{
			continue;
		}
		FDelegateHandle Handle;
		Watcher->RegisterDirectoryChangedCallback_Handle(
			Dir,
			IDirectoryWatcher::FDirectoryChanged::CreateUObject(this, &UMod_HotReloadSubsystem::OnDirectoryChanged),
			Handle,
			IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges);
		WatchHandles.Add(Dir, Handle);
	}

	UE_LOG(LogDP, Log, TEXT("ModContent: hot-reload watching %d director(ies)."), WatchHandles.Num());
}

void UMod_HotReloadSubsystem::StopWatching()
{
	if (WatchHandles.Num() == 0)
	{
		return;
	}
	if (FDirectoryWatcherModule* Module = FModuleManager::GetModulePtr<FDirectoryWatcherModule>(TEXT("DirectoryWatcher")))
	{
		if (IDirectoryWatcher* Watcher = Module->Get())
		{
			for (TPair<FString, FDelegateHandle>& Pair : WatchHandles)
			{
				Watcher->UnregisterDirectoryChangedCallback_Handle(Pair.Key, Pair.Value);
			}
		}
	}
	WatchHandles.Reset();
	bReloadPending = false;
}

void UMod_HotReloadSubsystem::OnDirectoryChanged(const TArray<FFileChangeData>& /*Changes*/)
{
	// Coalesce: (re)arm the debounce window rather than reloading on every file event.
	const UMod_DeveloperSettings* Settings = UMod_DeveloperSettings::Get();
	const float Configured = Settings ? Settings->HotReloadDebounceSeconds : 0.5f;
	DebounceRemainingSeconds = (Configured > 0.f) ? Configured : 0.5f; // defensive fallback
	bReloadPending = true;

	if (!DebounceTickerHandle.IsValid())
	{
		DebounceTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UMod_HotReloadSubsystem::TickDebounce));
	}
}

bool UMod_HotReloadSubsystem::TickDebounce(float DeltaTime)
{
	if (!bReloadPending)
	{
		DebounceTickerHandle.Reset();
		return false; // stop the ticker; it re-arms on the next change
	}

	DebounceRemainingSeconds -= DeltaTime;
	if (DebounceRemainingSeconds <= 0.0)
	{
		bReloadPending = false;
		PerformHotReload();
		DebounceTickerHandle.Reset();
		return false; // done; re-armed on next change
	}
	return true; // keep ticking down the window
}

void UMod_HotReloadSubsystem::PerformHotReload()
{
	UMod_ContentManagerSubsystem* Manager = nullptr;
	if (const UGameInstance* GI = GetGameInstance())
	{
		Manager = GI->GetSubsystem<UMod_ContentManagerSubsystem>();
	}
	if (!Manager)
	{
		return;
	}

	// Capture the currently-mounted set so we re-mount exactly what was active (PUBLIC API only).
	TArray<FGameplayTag> WasMounted;
	for (const FMod_MountedPack& Rec : Manager->GetMountedPacks())
	{
		WasMounted.Add(Rec.Info.PackId);
	}

	// Unmount in reverse order, re-discover from disk, then re-mount (validate-before-activate applies).
	for (int32 i = WasMounted.Num() - 1; i >= 0; --i)
	{
		Manager->UnmountPack(WasMounted[i]);
	}
	Manager->DiscoverPacks();
	for (const FGameplayTag& PackId : WasMounted)
	{
		Manager->MountPack(PackId);
	}

	UE_LOG(LogDP, Log, TEXT("ModContent: hot-reload re-discovered and re-mounted %d pack(s)."), WasMounted.Num());
}

void UMod_HotReloadSubsystem::HotReloadPack_Internal(FGameplayTag PackId, bool& bOutResult)
{
	bOutResult = false;
	UMod_ContentManagerSubsystem* Manager = nullptr;
	if (const UGameInstance* GI = GetGameInstance())
	{
		Manager = GI->GetSubsystem<UMod_ContentManagerSubsystem>();
	}
	if (!Manager || !PackId.IsValid())
	{
		return;
	}

	const bool bWasMounted = Manager->IsPackMounted(PackId);
	if (bWasMounted)
	{
		Manager->UnmountPack(PackId);
	}
	Manager->DiscoverPacks();
	bOutResult = bWasMounted ? Manager->MountPack(PackId) : true;
}

#endif // WITH_EDITOR

// =====================================================================================================
// Public API defined for ALL configs (inert no-op surface outside the editor)
// =====================================================================================================

#if !WITH_EDITOR
void UMod_HotReloadSubsystem::StartWatching() {}
void UMod_HotReloadSubsystem::StopWatching() {}
#endif

bool UMod_HotReloadSubsystem::HotReloadPack(FGameplayTag PackId)
{
#if WITH_EDITOR
	bool bResult = false;
	HotReloadPack_Internal(PackId, bResult);
	return bResult;
#else
	(void)PackId;
	return false;
#endif
}
