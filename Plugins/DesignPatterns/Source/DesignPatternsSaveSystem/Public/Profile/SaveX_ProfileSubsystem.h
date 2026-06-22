// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Storage/SaveX_StorageSubsystem.h" // ESaveX_StorageResult
#include "SaveX_ProfileSubsystem.generated.h"

class UDP_ProfileSaveGame;
class USaveX_StorageSubsystem;

/** Broadcast (game thread) after a profile save completes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSaveX_OnProfileSaved, bool, bSuccess);

/** Broadcast (game thread) after a profile load completes; profile is null unless bSuccess. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSaveX_OnProfileLoaded, bool, bSuccess, UDP_ProfileSaveGame*, Profile);

/**
 * Separates a single persistent PROFILE partition (settings/unlocks/cross-save shared data) from the
 * per-WORLD saves the slot manager handles.
 *
 * The profile is written through USaveX_StorageSubsystem to a reserved slot with the IsProfile container
 * flag. Gather pulls ONLY the ISeam_Persistable participants whose GetPersistenceKind() is in the
 * configured ProfilePersistenceKinds set; everything else is left for world saves so the two partitions
 * never overlap. Cross-save shared data (unlocks, shared records) is exposed so a world load can read
 * profile state, and MergeSharedDataIntoWorldSave lets a world save embed a snapshot of it.
 *
 * AUTHORITY: gather reads world state, so it is authority-guarded (no gather on a pure client). The
 * profile itself is GameInstance-scoped and holds NO replicated state.
 */
UCLASS()
class DESIGNPATTERNSSAVESYSTEM_API USaveX_ProfileSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Gather profile-kind participants into the cached profile and write it to the reserved profile slot. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Profile")
	void SaveProfile();

	/** Load the reserved profile slot into the cache (constructs an empty profile if none exists). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Profile")
	void LoadProfile();

	/** The cached profile object (constructed lazily; never null after Initialize). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|Profile")
	UDP_ProfileSaveGame* GetProfile() const { return CachedProfile; }

	/**
	 * Copy the profile's cross-save shared data (unlocks + shared records) into a world save so a world load
	 * can read profile unlocks without a separate profile read. Safe to call before SaveProfile.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Save|Profile")
	void MergeSharedDataIntoWorldSave(UDP_SaveGame* WorldSave) const;

	/** True if a kind belongs to the profile partition (per ProfilePersistenceKinds). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Save|Profile")
	bool IsProfileKind(FGameplayTag Kind) const;

	/** Broadcast after a profile save completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save|Profile")
	FSaveX_OnProfileSaved OnProfileSaved;

	/** Broadcast after a profile load completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Save|Profile")
	FSaveX_OnProfileLoaded OnProfileLoaded;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** The cached profile (constructed lazily, owned as a UPROPERTY so it survives across level travel). */
	UPROPERTY(Transient)
	TObjectPtr<UDP_ProfileSaveGame> CachedProfile;

private:
	/** Resolve the storage subsystem (the wrapped byte pipeline). Null in early-load contexts. */
	USaveX_StorageSubsystem* GetStorage() const;

	/** Construct a fresh profile object of the configured class (falls back to the base profile class). */
	UDP_ProfileSaveGame* ConstructProfileObject() const;

	/** Reserved profile slot name (from settings, with a stable fallback). */
	FString GetProfileSlotName() const;

	/** GAME THREAD + authority-guarded: gather profile-kind participants into the cached profile. */
	void GatherProfileParticipants();

	/** True if the GameInstance world has save authority (no gather on a pure client). */
	bool HasSaveAuthority() const;
};
