// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Share/Rep_ShareDescriptor.h"
#include "UObject/WeakInterfacePtr.h"
#include "Rep_ShareService.generated.h"

class ISeam_ReplayThumbnailSource;
class URep_HighlightSubsystem;
class URep_ReplayTimeline;
struct FSeam_ThumbnailHandle;

// ---- State of one pending share export ----------------------------------------

/**
 * Lifecycle state of an in-progress share export (from BeginShare through write-complete or timeout).
 *
 * PENDING     = thumbnail capture in flight (polling IsThumbnailReady).
 * WRITING     = thumbnail bytes resolved, share sidecar writing to disk (game-thread value snapshot
 *               handed to the async Async(ThreadPool) write).
 * COMPLETE    = sidecar written; descriptor is final and ThumbnailFilePath is populated.
 * FAILED      = write failed or thumbnail timed out — descriptor is still usable but lacks a thumbnail.
 */
UENUM(BlueprintType)
enum class ERep_ShareExportState : uint8
{
	/** Waiting for the async thumbnail capture to complete. */
	Pending,
	/** Thumbnail ready; writing sidecar to disk on a background thread. */
	Writing,
	/** Sidecar and (if available) thumbnail written — the share is ready to hand to the OS. */
	Complete,
	/** Export finished but a step failed (thumbnail timeout or disk write error). The descriptor is still valid. */
	Failed
};

/**
 * One in-flight or completed share entry tracked by the share service.
 * Flat + copyable so it can be surfaced to a view-model or UI without holding UObject refs.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_PendingShare
{
	GENERATED_BODY()

	/** The descriptor being built. ShareId is the stable key for this entry. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Share")
	FRep_ShareDescriptor Descriptor;

	/** Current lifecycle state. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Replay|Share")
	ERep_ShareExportState State = ERep_ShareExportState::Pending;

	/** Seconds spent waiting for the thumbnail (used to enforce the timeout from settings). */
	float ThumbnailWaitSeconds = 0.f;

	/** The thumbnail handle returned by the source when CaptureReplayThumbnail was called. */
	FSeam_ThumbnailHandle ThumbnailHandle;

	/** True when the thumbnail source was absent and we fell back to FScreenshotRequest. */
	bool bUsedScreenshotFallback = false;
};

// ---- Delegates ----------------------------------------------------------------

/** Fired when a share export completes (state = Complete or Failed). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRep_OnShareExportFinished, const FRep_ShareDescriptor&, Descriptor);

// ---- URep_ReplayShareService -------------------------------------------------

/**
 * URep_ReplayShareService — the local metadata + thumbnail export layer for shareable replays.
 *
 * RESPONSIBILITIES:
 *  - Build a FRep_ShareDescriptor for a replay / highlight reel (no networking — local only).
 *  - Request an async thumbnail via ISeam_ReplayThumbnailSource (resolved from the service locator under
 *    Rep_NativeTags::Service_Replay_Thumbnail). When no source is registered, falls back to the engine
 *    FScreenshotRequest::RequestScreenshot so a share card still gets a thumbnail without a bespoke adapter.
 *    The fallback is documented in the ISeam_ReplayThumbnailSource header's inert-default contract.
 *  - Polls pending captures each tick (via FTickableGameObject) and on completion serializes the descriptor
 *    sidecar (and optionally the raw thumbnail bytes) under Saved/Replays/Shares/<ShareId>/ using a
 *    game-thread value snapshot + Async(ThreadPool) file write — mirroring the off-thread pattern in
 *    UAnalytics_Subsystem::FlushToFileSink and URep_ReplaySubsystem's sidecar strategy.
 *  - Maintains a registry of pending + completed share descriptors queryable by the UI.
 *
 * CONSTRAINTS:
 *  - NO networking. No upload, no online service call, no FHttpModule. Local file/metadata only.
 *  - Off-thread file IO: all serialization leaves game-thread as a plain-value capture. The async block
 *    never closes over 'this' (it receives a snapshot + an absolute path by value).
 *  - Thumbnail timeout: if the async thumbnail is not ready within ShareThumbnailTimeoutSeconds (settings),
 *    the descriptor is written without a thumbnail rather than leaving the export pending forever.
 *  - Sidecar path: Saved/Replays/Shares/<ShareId>/<ReplayName>.share  (JSON-encoded, FBufferArchive).
 *    Thumbnail bytes (when present): Saved/Replays/Shares/<ShareId>/thumb.<ext>.
 *    These are parallel to the demo and timeline sidecars (under Saved/Replays) but in a distinct subtree
 *    so a future share-kit adapter can locate all share assets without traversing the demo directory.
 *
 * MP CAVEAT: this service runs on the LOCAL machine and never touches the server. A server-recorded demo
 * that was copied to the client can be shared from the client; any share authored from a client-recorded
 * session (discouraged — see URep_ReplaySubsystem) shares only local cosmetic state.
 *
 * GC: a GameInstance subsystem. Held TObjectPtr for owned share state. The thumbnail source is held as a
 * pruned-on-use TWeakInterfacePtr re-resolved each poll cycle. No cross-world strong refs.
 */
UCLASS()
class DESIGNPATTERNSREPLAY_API URep_ReplayShareService
	: public UDP_GameInstanceSubsystem
	, public FTickableGameObject
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Share authoring ------------------------------------------------------

	/**
	 * Begin building a share for a full replay (no specific clip window). An async thumbnail capture is
	 * started immediately. Returns the ShareId of the new pending entry; the caller can poll GetShare(id)
	 * or bind OnShareExportFinished. Returns an invalid FGuid if the replay name is empty.
	 *
	 * @param ReplayName  The demo stream name to share (stable id returned by URep_ReplaySubsystem::StartRecording).
	 * @param Title       Optional viewer-authored title for the share card.
	 * @param Caption     Optional description / caption.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Share")
	FGuid BeginShareReplay(const FString& ReplayName, const FText& Title, const FText& Caption);

	/**
	 * Begin building a share for a single clip window (a specific in/out time range). Same lifecycle as
	 * BeginShareReplay. The clip range is stamped into the descriptor; the actual clip encoding is out of
	 * scope (the descriptor tells a platform adapter where to cut).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Share")
	FGuid BeginShareClip(const FString& ReplayName, float ClipInSeconds, float ClipOutSeconds,
		const FText& Title, const FText& Caption);

	/**
	 * Begin building a share for a highlight reel. The reel is embedded in the descriptor so a consumer
	 * can play the moments back-to-back. A thumbnail is still captured from the current view.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Share")
	FGuid BeginShareReel(const FRep_HighlightReel& Reel, const FText& Title, const FText& Caption);

	// ---- Query ----------------------------------------------------------------

	/**
	 * Look up a pending or completed share by id. Returns false if the id is unknown (either never started
	 * or purged by PurgeCompleted). A share in Failed state is still returnable and has a valid descriptor.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Share")
	bool GetShare(const FGuid& ShareId, FRep_PendingShare& OutShare) const;

	/** All known shares (pending and completed) in order of creation. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Share")
	const TArray<FRep_PendingShare>& GetAllShares() const { return Shares; }

	/** Number of shares currently pending (state = Pending or Writing). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Replay|Share")
	int32 GetPendingCount() const;

	/** Remove all shares in Complete or Failed state, freeing memory. Does not delete on-disk sidecars. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Replay|Share")
	void PurgeCompleted();

	// ---- Delegates ------------------------------------------------------------

	/** Broadcast when a share export finishes (state becomes Complete or Failed). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Replay|Share")
	FRep_OnShareExportFinished OnShareExportFinished;

	// ---- FTickableGameObject --------------------------------------------------

	/** Drive the thumbnail poll loop and the timeout check. */
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual ETickableTickType GetTickableTickType() const override { return ETickableTickType::Conditional; }
	virtual UWorld* GetTickableGameObjectWorld() const override;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	// ---- Path helpers (public so tests / UIs can resolve share files) ---------

	/**
	 * Absolute path of the root directory for all share sidecars of a given share id.
	 * Pattern: <ProjectSaved>/Replays/Shares/<ShareId>/
	 * (mirrors GetSidecarPath logic in URep_ReplayTimeline but in a separate sub-tree).
	 */
	static FString GetShareDirectory(const FGuid& ShareId);

	/**
	 * Absolute path of the descriptor sidecar file for ShareId.
	 * Pattern: <ShareDirectory>/<ReplayName>.share
	 */
	static FString GetDescriptorPath(const FGuid& ShareId, const FString& ReplayName);

	/**
	 * Absolute path for thumbnail bytes for ShareId.
	 * Pattern: <ShareDirectory>/thumb.bin  (raw bytes as returned by ResolveThumbnailBytes)
	 */
	static FString GetThumbnailPath(const FGuid& ShareId);

private:
	// ---- Active share entries -------------------------------------------------

	/** All shares: pending, writing, complete, failed. */
	UPROPERTY(Transient)
	TArray<FRep_PendingShare> Shares;

	// ---- Seam (thumbnail source, pruned-on-use) --------------------------------

	/** The registered thumbnail source (if any). Resolved lazily; held weakly and pruned on use. */
	TWeakInterfacePtr<ISeam_ReplayThumbnailSource> ThumbnailSource;

	/** Resolve (and re-cache) the thumbnail source from the locator, or null. */
	ISeam_ReplayThumbnailSource* ResolveThumbnailSource();

	// ---- Internal share lifecycle --------------------------------------------

	/**
	 * Allocate a new FRep_PendingShare, stamp its descriptor, request a thumbnail (or fall back to
	 * FScreenshotRequest), and append it to Shares. Returns the new ShareId.
	 */
	FGuid StartShare(FRep_ShareDescriptor&& Descriptor);

	/**
	 * Request a thumbnail via the seam (or screenshot fallback) and store the resulting handle in the
	 * share entry at EntryIndex.
	 */
	void RequestThumbnailForEntry(int32 EntryIndex);

	/**
	 * Poll a single Pending entry: check IsThumbnailReady (or the screenshot fallback), enforce the
	 * timeout, and transition to Writing when the bytes are available.
	 */
	void PollEntry(FRep_PendingShare& Entry);

	/**
	 * Advance a Writing entry: verify the async write result (recorded on the writing-complete callback)
	 * and transition to Complete or Failed.
	 * Writing transitions are tracked via the bWriteSucceeded field on the entry (set on game thread
	 * from a deferred callback) so the tick loop can finalize in a single pass.
	 */
	void FinalizeWritingEntry(FRep_PendingShare& Entry);

	/**
	 * Fire off the async sidecar write for Entry: snapshot the descriptor + thumbnail bytes by value,
	 * pass to Async(ThreadPool), broadcast OnShareExportFinished on the game thread when done (via
	 * AsyncTask(GameThread)) and mark the entry Complete or Failed.
	 */
	void LaunchAsyncWrite(FRep_PendingShare& Entry, TArray<uint8>&& ThumbnailBytes);

	// ---- Screenshot fallback state -------------------------------------------
	// When the thumbnail seam is absent we use UGameViewportClient::OnScreenshotCaptured +
	// FScreenshotRequest (matching USaveX_ThumbnailCapturer's pattern; no RenderCore/RHI dep).
	// Only one screenshot capture is allowed in flight at a time; additional pending entries wait for
	// the timeout and write their descriptor without a thumbnail.

	/**
	 * Delegate handle for the UGameViewportClient::OnScreenshotCaptured binding so we can remove it
	 * on completion, timeout, or Deinitialize. Reset after each capture.
	 */
	FDelegateHandle ScreenshotDelegateHandle;

	/**
	 * True while a UGameViewportClient screenshot capture is in flight (ensures only one is bound at
	 * a time). Reset in HandleViewportScreenshot or on timeout.
	 */
	bool bCaptureInFlight = false;

	/**
	 * The ShareId of the entry that requested the current in-flight screenshot capture, so
	 * HandleViewportScreenshot can route the bitmap to the correct entry.
	 */
	FGuid PendingFallbackShareId;

	/**
	 * Callback from UGameViewportClient::OnScreenshotCaptured. Receives the raw BGRA bitmap,
	 * downscales it by value on a thread-pool thread, and hands it to LaunchAsyncWrite on the
	 * game thread. Removes the binding on first invocation (one-shot).
	 */
	void HandleViewportScreenshot(int32 Width, int32 Height, const TArray<FColor>& Bitmap);

	// ---- Helpers --------------------------------------------------------------

	/** Find the entry with the given ShareId; returns nullptr if not found. */
	FRep_PendingShare* FindEntry(const FGuid& ShareId);
	const FRep_PendingShare* FindEntry(const FGuid& ShareId) const;

	/** True while there is at least one entry in Pending or Writing state (drives conditional tick). */
	bool HasAnyPendingEntry() const;
};
