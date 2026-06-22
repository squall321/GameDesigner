// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Storage/SaveX_ContainerHeader.h" // ESaveX_Compression
#include "SaveX_StorageDeveloperSettings.generated.h"

/** Policy for how a cloud-vs-local conflict detected on load is resolved when no UI intervenes. */
UENUM(BlueprintType)
enum class ESaveX_CloudConflictPolicy : uint8
{
	/** Never auto-resolve; raise the conflict event and let UI decide (safest default). */
	Prompt,
	/** Always prefer the local copy on conflict (offline-first). */
	PreferLocal,
	/** Always prefer the remote copy on conflict (cloud-authoritative). */
	PreferRemote,
	/** Prefer whichever copy has the newer timestamp. */
	PreferNewest
};

/**
 * MECHANISM tunables for the additive SaveSystem storage layer (compression / encryption / corruption
 * safety / thumbnails / cloud).
 *
 * Appears under Project Settings -> Plugins -> Design Patterns Save Storage. This is a SIBLING of
 * USaveX_DeveloperSettings (which stays UNTOUCHED and owns the checkpoint/autosave/slot POLICY). The split
 * keeps the original policy settings stable while the new wrapper pipeline gets its own knobs. Thumbnail
 * POLICY (when to capture) still lives in USaveX_DeveloperSettings::ThumbnailPolicy; only the thumbnail
 * MECHANISM size lives here.
 *
 * Every gameplay/IO-affecting number is an EditAnywhere/Config field (no magic numbers in code). Consumers
 * null-check the CDO and fall back to documented defensive defaults so a missing settings object never
 * crashes the pipeline.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Save Storage"))
class DESIGNPATTERNSSAVESYSTEM_API USaveX_StorageDeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USaveX_StorageDeveloperSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	// ---- Compression ----

	/** Master switch for compressing the core blob before write. When false, the payload is stored verbatim. */
	UPROPERTY(EditAnywhere, Config, Category = "Compression")
	bool bCompressSaves = true;

	/**
	 * Preferred codec. Oodle is used only when the engine has it registered; otherwise the pipeline falls
	 * back to Zlib (always available) and records the codec actually used in the container header.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Compression", meta = (EditCondition = "bCompressSaves"))
	ESaveX_Compression CompressionMethod = ESaveX_Compression::Zlib;

	/**
	 * Minimum inner-blob size (bytes) below which compression is skipped (tiny blobs rarely shrink and the
	 * header overhead can make them larger). A defensive floor of 0 disables the threshold.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Compression", meta = (ClampMin = "0", UIMin = "0", EditCondition = "bCompressSaves"))
	int32 MinBytesToCompress = 256;

	// ---- Encryption ----

	/**
	 * When true, a save write FAILS (rather than falling back to plaintext) if no enabled ISeam_SaveCipher
	 * is registered. Use for titles with a hard encryption requirement. When false, a missing/disabled
	 * cipher simply produces a plaintext container.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Encryption")
	bool bRequireEncryption = false;

	/** Service-locator key under which the project publishes its ISeam_SaveCipher. */
	UPROPERTY(EditAnywhere, Config, Category = "Encryption")
	FGameplayTag CipherServiceTag;

	// ---- Corruption safety ----

	/**
	 * Number of rotating backups kept per slot (slot.dpcsav.bak0 .. bakN-1). On each successful write the
	 * previous good file is rotated into the backup ring; on a failed/corrupt load the pipeline recovers
	 * from the newest valid backup. 0 disables backups (the atomic temp+rename still protects the write).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Corruption Safety", meta = (ClampMin = "0", UIMin = "0", UIMax = "8"))
	int32 BackupRotationCount = 2;

	/**
	 * When true, a load whose primary file fails CRC/decrypt/decompress automatically attempts recovery
	 * from the backup ring before reporting failure. When false, a corrupt primary fails immediately.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Corruption Safety")
	bool bRecoverFromBackupOnCorruption = true;

	// ---- Thumbnails (MECHANISM only; policy lives in USaveX_DeveloperSettings) ----

	/**
	 * Longest-edge pixel size the captured screenshot is downscaled to before PNG encode. Keeps slot
	 * thumbnails small on disk. Clamped to a sane floor.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Thumbnail", meta = (ClampMin = "16", UIMin = "16", UIMax = "1024"))
	int32 ThumbnailMaxSize = 256;

	/**
	 * Seconds to wait for an async screenshot before giving up and writing the save without a thumbnail.
	 * Prevents a stalled capture from blocking the save indefinitely.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Thumbnail", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float ThumbnailCaptureTimeoutSeconds = 2.0f;

	// ---- Profile vs world partitioning ----

	/** Reserved slot name for the single persistent profile partition (cross-save shared data). */
	UPROPERTY(EditAnywhere, Config, Category = "Profile")
	FString ProfileSlotName = TEXT("DPProfile");

	/**
	 * The set of ISeam_Persistable::GetPersistenceKind() tags that belong to the PROFILE partition rather
	 * than a per-world save (e.g. SaveX.Persist.Kind.Profile.*). Participants whose kind matches any of
	 * these are gathered into the profile save; everything else stays in world saves.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Profile")
	FGameplayTagContainer ProfilePersistenceKinds;

	/** Concrete UDP_ProfileSaveGame subclass to instantiate for the profile partition (base used if unset). */
	UPROPERTY(EditAnywhere, Config, Category = "Profile", meta = (MetaClass = "/Script/DesignPatternsSaveSystem.DP_ProfileSaveGame"))
	FSoftClassPath ProfileSaveGameClass;

	// ---- Cloud ----

	/** Upload each wrapped container to the cloud store seam immediately after a successful local write. */
	UPROPERTY(EditAnywhere, Config, Category = "Cloud")
	bool bUploadToCloudOnSave = false;

	/** How a cloud-vs-local conflict detected on load is resolved when no UI intervenes. */
	UPROPERTY(EditAnywhere, Config, Category = "Cloud")
	ESaveX_CloudConflictPolicy CloudConflictPolicy = ESaveX_CloudConflictPolicy::Prompt;

	/** Service-locator key under which the project publishes its cloud store seam. */
	UPROPERTY(EditAnywhere, Config, Category = "Cloud")
	FGameplayTag CloudServiceTag;

	// ---- Accessors ----

	/** Convenience accessor (may return nullptr in unusual early-load / cooker contexts; callers null-check). */
	static const USaveX_StorageDeveloperSettings* Get();

	// ---- Validated accessors (apply defensive floors regardless of config edits) ----

	/** Backup ring count, floored to >= 0. */
	int32 GetEffectiveBackupCount() const { return FMath::Max(0, BackupRotationCount); }

	/** Thumbnail longest-edge size, floored to >= 16. */
	int32 GetEffectiveThumbnailMaxSize() const { return FMath::Max(16, ThumbnailMaxSize); }

	/** Profile slot name with a stable fallback if cleared. */
	FString GetEffectiveProfileSlotName() const { return ProfileSlotName.IsEmpty() ? TEXT("DPProfile") : ProfileSlotName; }
};
