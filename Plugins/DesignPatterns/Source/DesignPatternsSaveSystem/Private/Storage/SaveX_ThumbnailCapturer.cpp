// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Storage/SaveX_ThumbnailCapturer.h"

#include "Settings/SaveX_DeveloperSettings.h"
#include "Settings/SaveX_StorageDeveloperSettings.h"

#include "Core/DPLog.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "UnrealClient.h"                 // FScreenshotRequest
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"

void USaveX_ThumbnailCapturer::RequestThumbnailForSave(UObject* WorldContext, bool bIsAutosave, FSaveX_ThumbnailReady OnReady)
{
	check(IsInGameThread());

	auto FailEmpty = [&OnReady]()
	{
		TArray<uint8> Empty;
		OnReady.ExecuteIfBound(MoveTemp(Empty), 0, 0);
	};

	// Respect the EXISTING thumbnail policy. This helper does not own the decision.
	const USaveX_DeveloperSettings* Policy = USaveX_DeveloperSettings::Get();
	if (!Policy || !Policy->ShouldRequestThumbnail(bIsAutosave))
	{
		FailEmpty();
		return;
	}

	// Only one capture at a time; an overlapping request completes empty rather than stomping state.
	if (bCaptureInFlight)
	{
		UE_LOG(LogDPSave, Verbose, TEXT("[Thumbnail] Capture already in flight; new request returns empty."));
		FailEmpty();
		return;
	}

	// Find a viewport to screenshot. On a dedicated server / headless there is none — proceed without art.
	UGameViewportClient* Viewport = nullptr;
	if (WorldContext)
	{
		if (const UWorld* World = WorldContext->GetWorld())
		{
			Viewport = World->GetGameViewport();
		}
	}
	if (!Viewport || !GEngine)
	{
		UE_LOG(LogDPSave, Verbose, TEXT("[Thumbnail] No game viewport; saving without a thumbnail."));
		FailEmpty();
		return;
	}

	PendingOnReady = MoveTemp(OnReady);
	bCaptureInFlight = true;

	// Bind to the one-shot screenshot-captured delegate and request a non-UI screenshot.
	ScreenshotDelegateHandle = Viewport->OnScreenshotCaptured().AddUObject(this, &USaveX_ThumbnailCapturer::HandleScreenshotCaptured);
	FScreenshotRequest::RequestScreenshot(/*bShowUI=*/false);

	// Timeout guard so a stalled capture never blocks the save indefinitely.
	const USaveX_StorageDeveloperSettings* Storage = USaveX_StorageDeveloperSettings::Get();
	const float Timeout = Storage ? FMath::Max(0.f, Storage->ThumbnailCaptureTimeoutSeconds) : 2.0f;
	TWeakObjectPtr<USaveX_ThumbnailCapturer> WeakThis(this);
	TimeoutTickerHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[WeakThis](float /*Dt*/)
		{
			if (USaveX_ThumbnailCapturer* Self = WeakThis.Get())
			{
				if (Self->bCaptureInFlight)
				{
					UE_LOG(LogDPSave, Verbose, TEXT("[Thumbnail] Capture timed out; saving without a thumbnail."));
					TArray<uint8> Empty;
					Self->CompleteRequest(MoveTemp(Empty), 0, 0);
				}
			}
			return false; // one-shot
		}), Timeout);
}

void USaveX_ThumbnailCapturer::HandleScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap)
{
	check(IsInGameThread());
	if (!bCaptureInFlight)
	{
		return; // already completed (e.g. via timeout)
	}

	if (Width <= 0 || Height <= 0 || Bitmap.Num() < Width * Height)
	{
		TArray<uint8> Empty;
		CompleteRequest(MoveTemp(Empty), 0, 0);
		return;
	}

	const USaveX_StorageDeveloperSettings* Storage = USaveX_StorageDeveloperSettings::Get();
	const int32 MaxEdge = Storage ? Storage->GetEffectiveThumbnailMaxSize() : 256;

	// Downscale on the game thread (cheap; small target) then encode PNG off-thread.
	TArray<FColor> Resized;
	int32 OutW = 0;
	int32 OutH = 0;
	DownscaleBitmap(Bitmap, Width, Height, MaxEdge, Resized, OutW, OutH);

	TWeakObjectPtr<USaveX_ThumbnailCapturer> WeakThis(this);
	Async(EAsyncExecution::ThreadPool, [Resized = MoveTemp(Resized), OutW, OutH, WeakThis]() mutable
	{
		TArray<uint8> Png;
		IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const TSharedPtr<IImageWrapper> PngWrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
		if (PngWrapper.IsValid() && OutW > 0 && OutH > 0)
		{
			if (PngWrapper->SetRaw(Resized.GetData(), static_cast<int64>(Resized.Num()) * sizeof(FColor), OutW, OutH, ERGBFormat::BGRA, 8))
			{
				// GetCompressed returns TArray64<uint8> on UE5; copy into a 32-bit TArray for the container.
				const TArray64<uint8>& Compressed = PngWrapper->GetCompressed();
				Png.Append(Compressed.GetData(), static_cast<int32>(Compressed.Num()));
			}
		}

		// Hop back to the game thread to fire OnReady (it touches UObject-owned state).
		AsyncTask(ENamedThreads::GameThread, [Png = MoveTemp(Png), OutW, OutH, WeakThis]() mutable
		{
			if (USaveX_ThumbnailCapturer* Self = WeakThis.Get())
			{
				Self->CompleteRequest(MoveTemp(Png), OutW, OutH);
			}
		});
	});

	// Stop listening for further captures from this request; OnReady fires from the async chain.
	if (GEngine)
	{
		if (UGameViewportClient* Viewport = GEngine->GameViewport)
		{
			Viewport->OnScreenshotCaptured().Remove(ScreenshotDelegateHandle);
		}
	}
	ScreenshotDelegateHandle.Reset();
}

void USaveX_ThumbnailCapturer::CompleteRequest(TArray<uint8>&& PngBytes, int32 Width, int32 Height)
{
	check(IsInGameThread());

	// Remove the timeout ticker if still pending.
	if (TimeoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TimeoutTickerHandle);
		TimeoutTickerHandle.Reset();
	}
	// Defensive: ensure the screenshot delegate is unbound.
	if (ScreenshotDelegateHandle.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->OnScreenshotCaptured().Remove(ScreenshotDelegateHandle);
		ScreenshotDelegateHandle.Reset();
	}

	bCaptureInFlight = false;

	FSaveX_ThumbnailReady Local = MoveTemp(PendingOnReady);
	PendingOnReady.Unbind();
	Local.ExecuteIfBound(MoveTemp(PngBytes), Width, Height);
}

bool USaveX_ThumbnailCapturer::DecodePngDimensions(const TArray<uint8>& PngBytes, int32& OutW, int32& OutH) const
{
	OutW = 0;
	OutH = 0;
	if (PngBytes.Num() == 0)
	{
		return false;
	}
	IImageWrapperModule& WrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	const TSharedPtr<IImageWrapper> PngWrapper = WrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (PngWrapper.IsValid() && PngWrapper->SetCompressed(PngBytes.GetData(), PngBytes.Num()))
	{
		OutW = PngWrapper->GetWidth();
		OutH = PngWrapper->GetHeight();
		return OutW > 0 && OutH > 0;
	}
	return false;
}

void USaveX_ThumbnailCapturer::Shutdown()
{
	if (TimeoutTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TimeoutTickerHandle);
		TimeoutTickerHandle.Reset();
	}
	if (ScreenshotDelegateHandle.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->OnScreenshotCaptured().Remove(ScreenshotDelegateHandle);
		ScreenshotDelegateHandle.Reset();
	}
	bCaptureInFlight = false;
	PendingOnReady.Unbind();
}

void USaveX_ThumbnailCapturer::DownscaleBitmap(const TArray<FColor>& Src, int32 SrcW, int32 SrcH, int32 MaxEdge,
	TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
{
	OutPixels.Reset();
	if (SrcW <= 0 || SrcH <= 0 || Src.Num() < SrcW * SrcH || MaxEdge <= 0)
	{
		OutW = 0;
		OutH = 0;
		return;
	}

	// Preserve aspect ratio; only downscale (never upscale a small frame).
	const int32 LongestEdge = FMath::Max(SrcW, SrcH);
	const float Scale = (LongestEdge > MaxEdge) ? (static_cast<float>(MaxEdge) / static_cast<float>(LongestEdge)) : 1.0f;
	OutW = FMath::Max(1, FMath::RoundToInt(SrcW * Scale));
	OutH = FMath::Max(1, FMath::RoundToInt(SrcH * Scale));

	OutPixels.SetNumUninitialized(OutW * OutH);

	// Nearest-neighbour sampling: thumbnails are tiny, so a box/bilinear filter is unnecessary overhead.
	for (int32 Y = 0; Y < OutH; ++Y)
	{
		const int32 SrcY = FMath::Min(SrcH - 1, FMath::FloorToInt((Y + 0.5f) / Scale));
		for (int32 X = 0; X < OutW; ++X)
		{
			const int32 SrcX = FMath::Min(SrcW - 1, FMath::FloorToInt((X + 0.5f) / Scale));
			FColor Pixel = Src[SrcY * SrcW + SrcX];
			Pixel.A = 255; // force opaque so the thumbnail is not accidentally transparent
			OutPixels[Y * OutW + X] = Pixel;
		}
	}
}
