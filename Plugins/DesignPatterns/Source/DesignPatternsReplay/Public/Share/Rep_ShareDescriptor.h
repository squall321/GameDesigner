// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Highlight/Rep_HighlightTypes.h"
#include "Rep_ShareDescriptor.generated.h"

/**
 * A self-contained, networking-free descriptor of a shareable replay / highlight package.
 *
 * It is METADATA ONLY — it never uploads anything. It records what a share would consist of (the demo
 * stream name, an optional clip window or highlight reel, a title/caption, and the on-disk path of a
 * captured thumbnail once ready). A platform share-kit adapter (out of this module's scope) can read a
 * written descriptor and hand the referenced files to the OS share sheet.
 *
 * Flat + SaveGame-friendly so the share service can serialize it to a sidecar and a UI can list pending
 * shares across sessions.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSREPLAY_API FRep_ShareDescriptor
{
	GENERATED_BODY()

	/** Stable id for this share (so UI can address a pending/written share). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	FGuid ShareId;

	/** The demo stream name being shared. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	FString ReplayName;

	/** Optional viewer-authored title for the share card. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	FText Title;

	/** Optional caption / description. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	FText Caption;

	/** True when this share covers a single clip window (In/Out below); false => whole replay or a reel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	bool bIsClip = false;

	/** Clip in-point (demo seconds) when bIsClip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	float ClipInSeconds = 0.f;

	/** Clip out-point (demo seconds) when bIsClip. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	float ClipOutSeconds = 0.f;

	/** The highlight reel this share packages (empty for a single-clip or whole-replay share). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	FRep_HighlightReel Reel;

	/** On-disk path of the captured thumbnail image, once written. Empty while pending / unavailable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	FString ThumbnailFilePath;

	/** Wall-clock time the share descriptor was created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, SaveGame, Category = "DesignPatterns|Replay|Share")
	FDateTime CreatedAt;

	/** True when the descriptor names a replay (the minimum to be actionable). */
	bool IsValid() const { return !ReplayName.IsEmpty() && ShareId.IsValid(); }
};
