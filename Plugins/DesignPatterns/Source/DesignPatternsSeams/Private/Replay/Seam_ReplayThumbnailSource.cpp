// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Replay/Seam_ReplayThumbnailSource.h"

// INERT native defaults for the replay-thumbnail seam. With no real source registered a capture is
// refused (the handle is left invalid), nothing is ever ready, and resolving yields no bytes. The
// Replay share service detects the invalid handle and falls back to the engine FScreenshotRequest
// path, so a share card still gets a thumbnail without any adapter present.

void ISeam_ReplayThumbnailSource::CaptureReplayThumbnail_Implementation(
	FIntPoint /*RequestedSize*/, FSeam_ThumbnailHandle& OutHandle)
{
	// Default: decline — leave the handle invalid so the consumer uses its screenshot fallback.
	OutHandle = FSeam_ThumbnailHandle();
}

bool ISeam_ReplayThumbnailSource::IsThumbnailReady_Implementation(const FSeam_ThumbnailHandle& /*Handle*/) const
{
	// Default: nothing was ever captured.
	return false;
}

bool ISeam_ReplayThumbnailSource::ResolveThumbnailBytes_Implementation(
	const FSeam_ThumbnailHandle& /*Handle*/, TArray<uint8>& OutBytes) const
{
	// Default: no bytes available.
	OutBytes.Reset();
	return false;
}
