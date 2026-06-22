// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "SaveX_ThumbnailCapturer.generated.h"

/**
 * Engine-wrapper helper that captures an async screenshot at save time and encodes it to a PNG thumbnail
 * for storage in the container header.
 *
 * Owned by USaveX_StorageSubsystem. It WRAPS the engine screenshot pipeline (FScreenshotRequest +
 * UGameViewportClient::OnScreenshotCaptured) — it never reads the back-buffer directly, so it needs no
 * RHI/RenderCore dependency. The captured frame is downscaled to USaveX_StorageDeveloperSettings::
 * ThumbnailMaxSize and PNG-encoded off the game thread via IImageWrapperModule, then handed back through a
 * TFunction so the storage subsystem can append it to the container.
 *
 * DECISION: capture is requested only when the EXISTING USaveX_DeveloperSettings::ShouldRequestThumbnail
 * (bIsAutosave) says so — this helper does not own thumbnail policy.
 */
UCLASS()
class DESIGNPATTERNSSAVESYSTEM_API USaveX_ThumbnailCapturer : public UObject
{
	GENERATED_BODY()

public:
	/** Called with the encoded PNG bytes + pixel dimensions when a capture completes (or empty on failure/skip). */
	DECLARE_DELEGATE_ThreeParams(FSaveX_ThumbnailReady, TArray<uint8>&& /*PngBytes*/, int32 /*Width*/, int32 /*Height*/);

	/**
	 * Request a thumbnail for an imminent save. If policy (USaveX_DeveloperSettings::ShouldRequestThumbnail)
	 * declines, or no viewport exists, OnReady is invoked immediately with empty bytes so the save proceeds
	 * without a thumbnail. Otherwise an engine screenshot is requested; when it arrives the frame is
	 * downscaled + PNG-encoded off-thread and OnReady fires on the game thread. A configurable timeout
	 * guards against a stalled capture.
	 *
	 * @param WorldContext  Any object with a UWorld (used to find the viewport).
	 * @param bIsAutosave   Threaded to the thumbnail policy.
	 * @param OnReady       Completion delegate (always invoked exactly once, on the game thread).
	 */
	void RequestThumbnailForSave(UObject* WorldContext, bool bIsAutosave, FSaveX_ThumbnailReady OnReady);

	/** Decode a previously-stored PNG thumbnail (raw bytes) into out dimensions; convenience for UI preloads. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save")
	bool DecodePngDimensions(const TArray<uint8>& PngBytes, int32& OutW, int32& OutH) const;

	/** Tear down any pending screenshot binding/timer; called by the owning subsystem on Deinitialize. */
	void Shutdown();

private:
	/** Bound to UGameViewportClient::OnScreenshotCaptured for one capture; unbound after it fires. */
	void HandleScreenshotCaptured(int32 Width, int32 Height, const TArray<FColor>& Bitmap);

	/** Finish the current request (fire OnReady once) and clear pending state. */
	void CompleteRequest(TArray<uint8>&& PngBytes, int32 Width, int32 Height);

	/** Downscale a BGRA bitmap to a longest-edge target, returning the resized pixels + dimensions. */
	static void DownscaleBitmap(const TArray<FColor>& Src, int32 SrcW, int32 SrcH, int32 MaxEdge,
		TArray<FColor>& OutPixels, int32& OutW, int32& OutH);

	/** The single in-flight completion delegate (capture is serialized; one at a time). */
	FSaveX_ThumbnailReady PendingOnReady;

	/** Handle for the OnScreenshotCaptured binding so it can be removed after firing / on shutdown. */
	FDelegateHandle ScreenshotDelegateHandle;

	/** FTSTicker handle for the capture timeout; removed when the capture completes or on shutdown. */
	FTSTicker::FDelegateHandle TimeoutTickerHandle;

	/** True while a capture is in flight (rejects overlapping requests, which complete empty). */
	bool bCaptureInFlight = false;
};
