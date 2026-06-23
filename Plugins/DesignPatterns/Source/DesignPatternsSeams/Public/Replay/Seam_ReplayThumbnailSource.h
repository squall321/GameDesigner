// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_ReplayThumbnailSource.generated.h"

/**
 * An opaque, copyable handle to an in-flight (or completed) thumbnail capture request.
 *
 * PURE DATA — no UObject ref, no FInstancedStruct — so it can be stashed on a share descriptor, a
 * view-model, or passed across a deferred lambda safely. The producer (the thumbnail source adapter)
 * fills RequestId on CaptureReplayThumbnail and the consumer (Replay's share service) polls
 * IsThumbnailReady / ResolveThumbnailBytes with the same handle until the bytes are available.
 *
 * The capture is ALWAYS asynchronous: a frame's worth of pixels cannot be returned synchronously
 * from the render thread, so the seam never blocks. When NO source is registered the Replay module
 * falls back to the engine FScreenshotRequest path keyed by the same handle id.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_ThumbnailHandle
{
	GENERATED_BODY()

	/** Stable id assigned by the source when a capture is requested. Invalid => no request issued. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Replay")
	FGuid RequestId;

	/** The pixel dimensions that were requested (echoed so the consumer can size its decode). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Seam|Replay")
	FIntPoint RequestedSize = FIntPoint::ZeroValue;

	FSeam_ThumbnailHandle() = default;
	explicit FSeam_ThumbnailHandle(const FGuid& InId, const FIntPoint& InSize)
		: RequestId(InId), RequestedSize(InSize) {}

	/** True once a capture has been requested under this handle (RequestId assigned). */
	bool IsValid() const { return RequestId.IsValid(); }

	/** A fresh handle for a request of the given size. */
	static FSeam_ThumbnailHandle New(const FIntPoint& Size)
	{
		return FSeam_ThumbnailHandle(FGuid::NewGuid(), Size);
	}

	bool operator==(const FSeam_ThumbnailHandle& Other) const { return RequestId == Other.RequestId; }
	bool operator!=(const FSeam_ThumbnailHandle& Other) const { return !(*this == Other); }

	friend uint32 GetTypeHash(const FSeam_ThumbnailHandle& H) { return GetTypeHash(H.RequestId); }
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ReplayThumbnailSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * PROJECT-BRIDGE seam onto a system that can capture a still thumbnail of the current viewport (for
 * a replay/highlight share card).
 *
 * The Replay module's share service wants a small PNG/JPEG thumbnail to attach to a share descriptor
 * WITHOUT hard-depending on RenderCore/RHI or a project's bespoke screenshot pipeline. A thin adapter
 * (engine viewport, a render-target capture component, or a platform share kit) implements this seam
 * and self-registers a TScriptInterface<ISeam_ReplayThumbnailSource> under a service-locator key owned
 * by the Replay module (DP.Service.Replay.Thumbnail). Consumers resolve it WEAKLY and re-resolve on use.
 *
 * BlueprintNativeEvent UINTERFACE (project-bridge / UI-class seam house style — like
 * ISeam_ActivationGate and ISeam_ModCatalogSource) so it is resolvable as a TScriptInterface and a
 * project may implement it in Blueprint. The shipped default implementation here is INERT: a capture
 * is refused (the returned handle is invalid), nothing is ever ready, and resolve yields no bytes.
 *
 * INERT-DEFAULT CONTRACT: when CaptureReplayThumbnail leaves the handle invalid, the Replay share
 * service detects the absent source and falls back to the engine's FScreenshotRequest::RequestScreenshot
 * keyed by its own request id — so a share card still gets a thumbnail without any adapter present.
 *
 * THREADING: all calls are GAME-THREAD only. The capture is asynchronous; the consumer polls
 * IsThumbnailReady before ResolveThumbnailBytes. No method ever blocks on the render thread.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ReplayThumbnailSource
{
	GENERATED_BODY()

public:
	/**
	 * Begin an asynchronous capture of the current view at the requested pixel size. The implementer
	 * assigns OutHandle.RequestId (and echoes RequestedSize); leaving the handle invalid signals "I
	 * cannot capture" so the consumer uses its FScreenshotRequest fallback. Never blocks.
	 *
	 * @param RequestedSize  Desired thumbnail dimensions in pixels (the implementer may downscale).
	 * @param OutHandle      Receives the handle to poll; left invalid if the source declines.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Replay")
	void CaptureReplayThumbnail(FIntPoint RequestedSize, UPARAM(ref) FSeam_ThumbnailHandle& OutHandle);

	/** True once the capture for Handle has completed and its bytes can be resolved. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Replay")
	bool IsThumbnailReady(const FSeam_ThumbnailHandle& Handle) const;

	/**
	 * Copy the completed, ENCODED image bytes (PNG/JPEG) for Handle into OutBytes. Returns true on
	 * success; false (and OutBytes left empty) if the capture is not ready or the handle is unknown.
	 * The byte format is the implementer's choice; the share service writes them verbatim to disk.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Replay")
	bool ResolveThumbnailBytes(const FSeam_ThumbnailHandle& Handle, UPARAM(ref) TArray<uint8>& OutBytes) const;
};
