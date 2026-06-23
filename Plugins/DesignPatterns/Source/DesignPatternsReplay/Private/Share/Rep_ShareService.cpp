// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Share/Rep_ShareService.h"
#include "Settings/Rep_DeveloperSettings.h"
#include "DesignPatternsReplayModule.h"

#include "Core/DPLog.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Replay/Seam_ReplayThumbnailSource.h"

#include "Engine/GameInstance.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "UnrealClient.h"          // FScreenshotRequest, UGameViewportClient::OnScreenshotCaptured
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Serialization/BufferArchive.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"

// ---------------------------------------------------------------------------
// URep_ReplayShareService  —  Initialize / Deinitialize
// ---------------------------------------------------------------------------

void URep_ReplayShareService::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogDP, Log, TEXT("URep_ReplayShareService initialized."));
}

void URep_ReplayShareService::Deinitialize()
{
	// Any in-flight screenshot bindings should be removed. Walk pending entries that used the fallback
	// and ensure the delegate is unbound. The game-viewport may already be gone at this point; guard.
	if (ScreenshotDelegateHandle.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->OnScreenshotCaptured().Remove(ScreenshotDelegateHandle);
		}
		ScreenshotDelegateHandle.Reset();
	}

	// Drop cached weak seam ref.
	ThumbnailSource.Reset();

	// Clear all state; any in-flight async writes hold snapshot copies and do not reference 'this'.
	Shares.Empty();

	Super::Deinitialize();
}

// ---------------------------------------------------------------------------
// Share authoring
// ---------------------------------------------------------------------------

FGuid URep_ReplayShareService::BeginShareReplay(const FString& ReplayName, const FText& Title,
	const FText& Caption)
{
	if (ReplayName.IsEmpty())
	{
		UE_LOG(LogDP, Warning, TEXT("ShareService: BeginShareReplay called with empty ReplayName; ignored."));
		return FGuid();
	}

	FRep_ShareDescriptor Desc;
	Desc.ShareId    = FGuid::NewGuid();
	Desc.ReplayName = ReplayName;
	Desc.Title      = Title;
	Desc.Caption    = Caption;
	Desc.bIsClip    = false;
	Desc.CreatedAt  = FDateTime::UtcNow();

	return StartShare(MoveTemp(Desc));
}

FGuid URep_ReplayShareService::BeginShareClip(const FString& ReplayName,
	float ClipInSeconds, float ClipOutSeconds,
	const FText& Title, const FText& Caption)
{
	if (ReplayName.IsEmpty())
	{
		UE_LOG(LogDP, Warning, TEXT("ShareService: BeginShareClip called with empty ReplayName; ignored."));
		return FGuid();
	}

	FRep_ShareDescriptor Desc;
	Desc.ShareId       = FGuid::NewGuid();
	Desc.ReplayName    = ReplayName;
	Desc.Title         = Title;
	Desc.Caption       = Caption;
	Desc.bIsClip       = true;
	Desc.ClipInSeconds = ClipInSeconds;
	Desc.ClipOutSeconds= ClipOutSeconds;
	Desc.CreatedAt     = FDateTime::UtcNow();

	return StartShare(MoveTemp(Desc));
}

FGuid URep_ReplayShareService::BeginShareReel(const FRep_HighlightReel& Reel,
	const FText& Title, const FText& Caption)
{
	if (Reel.ReplayName.IsEmpty())
	{
		UE_LOG(LogDP, Warning, TEXT("ShareService: BeginShareReel called with empty Reel.ReplayName; ignored."));
		return FGuid();
	}

	FRep_ShareDescriptor Desc;
	Desc.ShareId    = FGuid::NewGuid();
	Desc.ReplayName = Reel.ReplayName;
	Desc.Title      = Title;
	Desc.Caption    = Caption;
	Desc.bIsClip    = false;
	Desc.Reel       = Reel;
	Desc.CreatedAt  = FDateTime::UtcNow();

	return StartShare(MoveTemp(Desc));
}

// ---------------------------------------------------------------------------
// Query
// ---------------------------------------------------------------------------

bool URep_ReplayShareService::GetShare(const FGuid& ShareId, FRep_PendingShare& OutShare) const
{
	const FRep_PendingShare* Entry = FindEntry(ShareId);
	if (!Entry)
	{
		return false;
	}
	OutShare = *Entry;
	return true;
}

int32 URep_ReplayShareService::GetPendingCount() const
{
	int32 Count = 0;
	for (const FRep_PendingShare& S : Shares)
	{
		if (S.State == ERep_ShareExportState::Pending || S.State == ERep_ShareExportState::Writing)
		{
			++Count;
		}
	}
	return Count;
}

void URep_ReplayShareService::PurgeCompleted()
{
	Shares.RemoveAll([](const FRep_PendingShare& S)
	{
		return S.State == ERep_ShareExportState::Complete || S.State == ERep_ShareExportState::Failed;
	});
}

// ---------------------------------------------------------------------------
// FTickableGameObject
// ---------------------------------------------------------------------------

void URep_ReplayShareService::Tick(float DeltaTime)
{
	// Drive the poll loop for all Pending entries.
	for (FRep_PendingShare& Entry : Shares)
	{
		if (Entry.State == ERep_ShareExportState::Pending)
		{
			Entry.ThumbnailWaitSeconds += DeltaTime;
			PollEntry(Entry);
		}
		// Writing transitions happen asynchronously via the game-thread callback from LaunchAsyncWrite.
		// No tick action needed for Writing entries.
	}
}

TStatId URep_ReplayShareService::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(URep_ReplayShareService, STATGROUP_Tickables);
}

bool URep_ReplayShareService::IsTickable() const
{
	// Only tick while at least one entry is Pending (writing entries are passive / callback-driven).
	return HasAnyPendingEntry();
}

UWorld* URep_ReplayShareService::GetTickableGameObjectWorld() const
{
	if (const UGameInstance* GI = GetGameInstance())
	{
		return GI->GetWorld();
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Path helpers
// ---------------------------------------------------------------------------

FString URep_ReplayShareService::GetShareDirectory(const FGuid& ShareId)
{
	// Saved/Replays/Shares/<ShareId>/
	// Parallel to the demo timeline sidecars under Saved/Replays but in a distinct subtree so a
	// future share-kit adapter can enumerate all share assets without traversing the demo directory.
	return FPaths::Combine(
		FPaths::ProjectSavedDir(),
		TEXT("Replays"),
		TEXT("Shares"),
		ShareId.ToString(EGuidFormats::DigitsWithHyphens));
}

FString URep_ReplayShareService::GetDescriptorPath(const FGuid& ShareId, const FString& ReplayName)
{
	return FPaths::Combine(GetShareDirectory(ShareId), ReplayName + TEXT(".share"));
}

FString URep_ReplayShareService::GetThumbnailPath(const FGuid& ShareId)
{
	// Raw bytes as returned by ResolveThumbnailBytes (PNG/JPEG, implementer's choice).
	return FPaths::Combine(GetShareDirectory(ShareId), TEXT("thumb.bin"));
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------

FString URep_ReplayShareService::GetDPDebugString_Implementation() const
{
	const int32 Total   = Shares.Num();
	const int32 Pending = GetPendingCount();
	return FString::Printf(TEXT("ShareService shares=%d pending=%d"), Total, Pending);
}

// ---------------------------------------------------------------------------
// Internal lifecycle
// ---------------------------------------------------------------------------

FGuid URep_ReplayShareService::StartShare(FRep_ShareDescriptor&& Descriptor)
{
	const FGuid ShareId = Descriptor.ShareId;

	// Create the output directory eagerly so the screenshot fallback path is valid from the start.
	const FString Dir = GetShareDirectory(ShareId);
	IFileManager::Get().MakeDirectory(*Dir, /*Tree*/ true);

	FRep_PendingShare Entry;
	Entry.Descriptor = MoveTemp(Descriptor);
	Entry.State      = ERep_ShareExportState::Pending;

	const int32 EntryIndex = Shares.Add(MoveTemp(Entry));

	// Begin the async thumbnail capture immediately.
	RequestThumbnailForEntry(EntryIndex);

	UE_LOG(LogDP, Log, TEXT("ShareService: started share %s for replay '%s'."),
		*ShareId.ToString(), *Shares[EntryIndex].Descriptor.ReplayName);

	return ShareId;
}

void URep_ReplayShareService::RequestThumbnailForEntry(int32 EntryIndex)
{
	if (!Shares.IsValidIndex(EntryIndex))
	{
		return;
	}

	FRep_PendingShare& Entry = Shares[EntryIndex];

	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	const FIntPoint DesiredSize = Settings ? Settings->ShareThumbnailSize : FIntPoint(640, 360);

	// --- Primary path: ISeam_ReplayThumbnailSource ---
	// If the project registered an adapter under the thumbnail service key, it handles the capture.
	ISeam_ReplayThumbnailSource* Source = ResolveThumbnailSource();
	if (Source)
	{
		UObject* SourceObj = ThumbnailSource.GetObject();
		if (SourceObj)
		{
			FSeam_ThumbnailHandle Handle;
			ISeam_ReplayThumbnailSource::Execute_CaptureReplayThumbnail(SourceObj, DesiredSize, Handle);
			if (Handle.IsValid())
			{
				Entry.ThumbnailHandle       = Handle;
				Entry.bUsedScreenshotFallback = false;
				UE_LOG(LogDP, Verbose, TEXT("ShareService: thumbnail capture via seam for share %s."),
					*Entry.Descriptor.ShareId.ToString());
				return;
			}
		}
	}

	// --- Fallback path: UGameViewportClient::OnScreenshotCaptured + FScreenshotRequest ---
	// The seam declined (or is absent), so we use the engine screenshot pipeline directly.
	// Mirroring USaveX_ThumbnailCapturer (SaveSystem module): bind to OnScreenshotCaptured once,
	// request a non-UI screenshot, and receive the raw bitmap in the callback.
	// We allow only one screenshot binding at a time: if one is already in flight (bCaptureInFlight),
	// we skip the screenshot and let the poll timeout write without a thumbnail — correct behaviour
	// when multiple shares are started in rapid succession.
	if (!bCaptureInFlight)
	{
		UGameViewportClient* Viewport = nullptr;
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UWorld* World = GI->GetWorld())
			{
				Viewport = World->GetGameViewport();
			}
		}

		if (Viewport)
		{
			bCaptureInFlight            = true;
			PendingFallbackShareId      = Entry.Descriptor.ShareId;
			Entry.bUsedScreenshotFallback = true;

			// Bind once; the callback removes itself.
			ScreenshotDelegateHandle = Viewport->OnScreenshotCaptured().AddUObject(
				this, &URep_ReplayShareService::HandleViewportScreenshot);
			FScreenshotRequest::RequestScreenshot(/*bShowUI=*/false);

			UE_LOG(LogDP, Verbose, TEXT("ShareService: screenshot fallback requested for share %s."),
				*Entry.Descriptor.ShareId.ToString());
		}
		else
		{
			// No viewport (headless / dedicated server): write without a thumbnail immediately rather
			// than accumulating pending time. Mark as still Pending (PollEntry will timeout soon).
			Entry.bUsedScreenshotFallback = true;
			UE_LOG(LogDP, Verbose,
				TEXT("ShareService: no game viewport; share %s will write without thumbnail."),
				*Entry.Descriptor.ShareId.ToString());
		}
	}
	else
	{
		// A screenshot is already in flight; this entry will be served by PollEntry timeout.
		Entry.bUsedScreenshotFallback = true;
		UE_LOG(LogDP, Verbose,
			TEXT("ShareService: screenshot already in flight; share %s will wait for timeout."),
			*Entry.Descriptor.ShareId.ToString());
	}
}

void URep_ReplayShareService::HandleViewportScreenshot(int32 Width, int32 Height,
	const TArray<FColor>& Bitmap)
{
	check(IsInGameThread());

	// Unbind immediately — this delegate fires once per screenshot request.
	if (ScreenshotDelegateHandle.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
		{
			GEngine->GameViewport->OnScreenshotCaptured().Remove(ScreenshotDelegateHandle);
		}
		ScreenshotDelegateHandle.Reset();
	}

	bCaptureInFlight = false;

	// Find the entry this screenshot was for.
	FRep_PendingShare* EntryPtr = FindEntry(PendingFallbackShareId);
	PendingFallbackShareId.Invalidate();
	if (!EntryPtr || EntryPtr->State != ERep_ShareExportState::Pending)
	{
		return;
	}

	// Accept or reject the bitmap.
	if (Width <= 0 || Height <= 0 || Bitmap.Num() < Width * Height)
	{
		// Bad capture — fall through to timeout path (the entry stays Pending and times out).
		UE_LOG(LogDP, Warning, TEXT("ShareService: screenshot captured but invalid bitmap (%dx%d)."), Width, Height);
		return;
	}

	// Snapshot by value for off-thread PNG encode — never capture 'this'.
	TArray<FColor> BitmapCopy = Bitmap;
	const FGuid ShareId = EntryPtr->Descriptor.ShareId;

	// Encode on a thread-pool thread; hop back to game thread when done.
	Async(EAsyncExecution::ThreadPool,
		[BitmapCopy = MoveTemp(BitmapCopy), Width, Height, ShareId]() mutable
	{
		// Minimal nearest-neighbour downscale to cap the stored thumbnail size.
		// We target a longest-edge of 640 (same as the seam's DesiredSize) for consistency.
		const int32 MaxEdge = 640;
		const int32 LongestEdge = FMath::Max(Width, Height);
		const float Scale = (LongestEdge > MaxEdge)
			? (static_cast<float>(MaxEdge) / static_cast<float>(LongestEdge))
			: 1.0f;
		const int32 OutW = FMath::Max(1, FMath::RoundToInt(Width  * Scale));
		const int32 OutH = FMath::Max(1, FMath::RoundToInt(Height * Scale));

		TArray<uint8> Bytes;
		// Encode as raw BGRA bytes (simple, no additional module dep beyond Engine).
		// A platform share-kit adapter can decode / re-encode as needed.
		Bytes.SetNumUninitialized(OutW * OutH * 4);
		for (int32 Y = 0; Y < OutH; ++Y)
		{
			const int32 SrcY = FMath::Min(Height - 1, FMath::FloorToInt((Y + 0.5f) / Scale));
			for (int32 X = 0; X < OutW; ++X)
			{
				const int32 SrcX = FMath::Min(Width - 1, FMath::FloorToInt((X + 0.5f) / Scale));
				const FColor& Px = BitmapCopy[SrcY * Width + SrcX];
				const int32 Dst = (Y * OutW + X) * 4;
				Bytes[Dst + 0] = Px.B;
				Bytes[Dst + 1] = Px.G;
				Bytes[Dst + 2] = Px.R;
				Bytes[Dst + 3] = 255; // force opaque
			}
		}

		AsyncTask(ENamedThreads::GameThread,
			[ShareId, Bytes = MoveTemp(Bytes)]() mutable
		{
			// Re-find the service on the game thread.
			if (!GEngine)
			{
				return;
			}
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				UGameInstance* GI = Ctx.OwningGameInstance;
				if (!GI)
				{
					continue;
				}
				URep_ReplayShareService* Service = GI->GetSubsystem<URep_ReplayShareService>();
				if (!Service)
				{
					continue;
				}
				FRep_PendingShare* Entry = Service->FindEntry(ShareId);
				if (!Entry || Entry->State != ERep_ShareExportState::Pending)
				{
					continue;
				}
				// Mark thumbnail ready and launch the sidecar write.
				Service->LaunchAsyncWrite(*Entry, MoveTemp(Bytes));
				break;
			}
		});
	});
}

void URep_ReplayShareService::PollEntry(FRep_PendingShare& Entry)
{
	const URep_DeveloperSettings* Settings = URep_DeveloperSettings::Get();
	const float TimeoutSeconds = Settings ? Settings->ShareThumbnailTimeoutSeconds : 5.f;

	// --- Seam path: poll IsThumbnailReady ---
	if (!Entry.bUsedScreenshotFallback && Entry.ThumbnailHandle.IsValid())
	{
		ISeam_ReplayThumbnailSource* Source = ResolveThumbnailSource();
		UObject* SourceObj = ThumbnailSource.GetObject();
		if (Source && SourceObj)
		{
			const bool bReady =
				ISeam_ReplayThumbnailSource::Execute_IsThumbnailReady(SourceObj, Entry.ThumbnailHandle);
			if (bReady)
			{
				TArray<uint8> Bytes;
				const bool bGot =
					ISeam_ReplayThumbnailSource::Execute_ResolveThumbnailBytes(SourceObj, Entry.ThumbnailHandle, Bytes);
				if (bGot)
				{
					LaunchAsyncWrite(Entry, MoveTemp(Bytes));
					return;
				}
			}
		}
	}

	// --- Timeout: write without a thumbnail rather than stalling forever ---
	if (Entry.ThumbnailWaitSeconds >= FMath::Max(0.5f, TimeoutSeconds))
	{
		UE_LOG(LogDP, Warning,
			TEXT("ShareService: thumbnail timed out for share %s (%.1fs). Writing descriptor without thumbnail."),
			*Entry.Descriptor.ShareId.ToString(), Entry.ThumbnailWaitSeconds);
		TArray<uint8> EmptyBytes;
		LaunchAsyncWrite(Entry, MoveTemp(EmptyBytes));
	}
}

void URep_ReplayShareService::LaunchAsyncWrite(FRep_PendingShare& Entry, TArray<uint8>&& ThumbnailBytes)
{
	// Snapshot everything by value so the async block captures nothing from 'this'.
	FRep_ShareDescriptor DescSnapshot = Entry.Descriptor;
	TArray<uint8>        BytesSnapshot = MoveTemp(ThumbnailBytes);

	// Transition immediately to Writing so the poll loop skips this entry.
	Entry.State = ERep_ShareExportState::Writing;

	// Compute paths from the id alone (pure function, no UObject access).
	const FString DescPath  = GetDescriptorPath(DescSnapshot.ShareId, DescSnapshot.ReplayName);
	const FString ThumbPath = GetThumbnailPath(DescSnapshot.ShareId);
	const FGuid   CapId     = DescSnapshot.ShareId;

	Async(EAsyncExecution::ThreadPool,
		[DescSnapshot = MoveTemp(DescSnapshot), BytesSnapshot = MoveTemp(BytesSnapshot),
		 DescPath, ThumbPath, CapId]() mutable
	{
		bool bDescOk  = false;
		bool bHadThumb= (BytesSnapshot.Num() > 0);

		// --- Serialize the descriptor via FBufferArchive (binary archive) ---
		{
			FBufferArchive DescBuf;
			DescBuf << DescSnapshot;
			bDescOk = FFileHelper::SaveArrayToFile(DescBuf, *DescPath);
			if (!bDescOk)
			{
				UE_LOG(LogDP, Error,
					TEXT("ShareService: failed to write descriptor to '%s'."), *DescPath);
			}
		}

		// --- Write thumbnail bytes if present ---
		if (bHadThumb)
		{
			if (!FFileHelper::SaveArrayToFile(BytesSnapshot, *ThumbPath))
			{
				UE_LOG(LogDP, Warning,
					TEXT("ShareService: failed to write thumbnail to '%s'."), *ThumbPath);
				bHadThumb = false;
			}
		}

		const bool bFinalOk = bDescOk;

		// Return to the game thread to update entry state and broadcast.
		AsyncTask(ENamedThreads::GameThread,
			[CapId, bFinalOk, bHadThumb, ThumbPath]()
		{
			if (!GEngine)
			{
				return;
			}
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				UGameInstance* GI = Ctx.OwningGameInstance;
				if (!GI)
				{
					continue;
				}
				URep_ReplayShareService* Service = GI->GetSubsystem<URep_ReplayShareService>();
				if (!Service)
				{
					continue;
				}
				FRep_PendingShare* Entry = Service->FindEntry(CapId);
				if (!Entry)
				{
					continue;
				}
				if (bFinalOk && bHadThumb)
				{
					Entry->Descriptor.ThumbnailFilePath = ThumbPath;
				}
				Entry->State = bFinalOk
					? ERep_ShareExportState::Complete
					: ERep_ShareExportState::Failed;

				UE_LOG(LogDP, Log, TEXT("ShareService: share %s export %s."),
					*CapId.ToString(), bFinalOk ? TEXT("complete") : TEXT("failed"));

				Service->OnShareExportFinished.Broadcast(Entry->Descriptor);
				break;
			}
		});
	});
}

// ---------------------------------------------------------------------------
// Thumbnail seam resolution
// ---------------------------------------------------------------------------

ISeam_ReplayThumbnailSource* URep_ReplayShareService::ResolveThumbnailSource()
{
	if (ThumbnailSource.IsValid())
	{
		return ThumbnailSource.Get();
	}
	ThumbnailSource.Reset();

	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		return nullptr;
	}
	UDP_ServiceLocatorSubsystem* Locator = GI->GetSubsystem<UDP_ServiceLocatorSubsystem>();
	if (!Locator)
	{
		return nullptr;
	}

	UObject* Provider = Locator->ResolveService(Rep_NativeTags::Service_Replay_Thumbnail);
	if (Provider && Provider->GetClass()->ImplementsInterface(USeam_ReplayThumbnailSource::StaticClass()))
	{
		ThumbnailSource = TWeakInterfacePtr<ISeam_ReplayThumbnailSource>(Provider);
		if (ThumbnailSource.IsValid())
		{
			return ThumbnailSource.Get();
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------
// Lookup helpers
// ---------------------------------------------------------------------------

FRep_PendingShare* URep_ReplayShareService::FindEntry(const FGuid& ShareId)
{
	return Shares.FindByPredicate([&ShareId](const FRep_PendingShare& S)
	{
		return S.Descriptor.ShareId == ShareId;
	});
}

const FRep_PendingShare* URep_ReplayShareService::FindEntry(const FGuid& ShareId) const
{
	return Shares.FindByPredicate([&ShareId](const FRep_PendingShare& S)
	{
		return S.Descriptor.ShareId == ShareId;
	});
}

bool URep_ReplayShareService::HasAnyPendingEntry() const
{
	for (const FRep_PendingShare& S : Shares)
	{
		if (S.State == ERep_ShareExportState::Pending)
		{
			return true;
		}
	}
	return false;
}
