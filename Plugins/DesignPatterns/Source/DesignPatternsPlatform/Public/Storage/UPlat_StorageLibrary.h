// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UPlat_StorageLibrary.generated.h"

/**
 * A resolved, platform-correct location for save data plus the metadata the core Save system
 * needs to write to it safely. Returned by UPlat_StorageLibrary::ResolveSavePath.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPLATFORM_API FPlat_SavePath
{
	GENERATED_BODY()

	/** Absolute directory the save file should live in (always ends with a separator). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Storage")
	FString Directory;

	/** Sanitised, length-safe slot file name (no extension). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Storage")
	FString SanitizedSlotName;

	/** Full absolute path = Directory + SanitizedSlotName (+ extension supplied by caller). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Storage")
	FString FullPath;

	/** True when this platform routes the save directory through a cloud-backed location. */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Storage")
	bool bCloudSaveAware = false;
};

/**
 * Pure helpers for per-platform save-path resolution and slot-name sanitisation.
 *
 * The host project feeds the resolved FPlat_SavePath into the core Save system so the save
 * code itself stays platform-agnostic. All platform branching for the base directory and
 * cloud-awareness lives here. Slot names are sanitised to be filesystem- and length-safe so
 * a user-typed slot name can never produce an illegal or over-long path on any platform.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_StorageLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * The correct base save directory for this platform.
	 *  - Desktop: FPlatformProcess::UserSettingsDir() (per-user, roams/syncs on supported OSes).
	 *  - Mobile/console/other: FPaths::ProjectSavedDir() (sandboxed, platform-managed).
	 * Always returned with a trailing separator.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Storage")
	static FString GetPlatformSaveDirectory();

	/** True when the platform's save directory is backed by a cloud/roaming sync. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Storage")
	static bool IsCloudSaveAware();

	/**
	 * Make an arbitrary (possibly user-typed) slot name safe to use as a file name on every
	 * platform: strips path separators and illegal characters, collapses whitespace to '_',
	 * and clamps the length so the final absolute path stays well under platform limits.
	 * Never returns an empty string (falls back to "save").
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Storage")
	static FString SanitizeSlotName(const FString& RawSlotName);

	/**
	 * Resolve everything the core Save system needs for a given slot: platform directory,
	 * sanitised slot name, the joined full path, and the cloud-awareness flag.
	 * @param RawSlotName  Caller/user supplied slot name (unsanitised).
	 * @param Extension    File extension WITHOUT a leading dot (default "sav").
	 */
	UFUNCTION(BlueprintCallable, Category = "Platform|Storage", meta = (AdvancedDisplay = "Extension"))
	static FPlat_SavePath ResolveSavePath(const FString& RawSlotName, const FString& Extension = TEXT("sav"));

	/** Maximum length we allow for a sanitised slot file name (excludes directory + extension). */
	static constexpr int32 MaxSlotNameLength = 80;
};
