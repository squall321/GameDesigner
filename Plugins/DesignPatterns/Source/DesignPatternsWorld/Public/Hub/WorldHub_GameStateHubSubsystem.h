// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"
#include "Registry/WorldHub_FlagTypes.h"
#include "Registry/WorldHub_FlagRegistry.h"
#include "Save/WorldHub_Snapshot.h"
#include "Save/DPSaveGameSubsystem.h"
#include "WorldHub_GameStateHubSubsystem.generated.h"

class UWorldHub_StateHubSubsystem;
class UWorldHub_SaveGame;

/** Fired after the persistent hub is loaded from (or saved to) a slot, on the game thread. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWorldHub_OnPersistentIO, FString, Slot, EDP_SaveResult, Result);

/**
 * The cross-level, GameInstance-scoped owner of persistent world-hub state.
 *
 * World subsystems die with their world, so anything that must survive level travel is flushed into
 * THIS subsystem. It keeps a flat (Scope, Key) -> FWorldHub_FlagValue map (only save-bearing slots
 * are flushed into it), seeds a fresh world's hub from that map on level load, receives the world's
 * flush before travel, and bridges to the core save subsystem (UDP_SaveGameSubsystem) to build/apply
 * a UWorldHub_SaveGame.
 *
 * This subsystem is NEVER replicated and holds NO authority API for live world state — it is a pure
 * persistence cache. Authoritative live mutation lives on the world hub and its rep carrier.
 */
UCLASS()
class DESIGNPATTERNSWORLD_API UWorldHub_GameStateHubSubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// ---- Persistent key/value -----------------------------------------------------------------

	/** Store (or overwrite) a persistent value for (Key, Scope). */
	void SetPersistent(const FGameplayTag& Key, const FWorldHub_Scope& Scope, const FWorldHub_FlagValue& Value);

	/** Read a persistent value for (Key, Scope). @return true (and fills Out) if present. */
	bool GetPersistent(const FGameplayTag& Key, const FWorldHub_Scope& Scope, FWorldHub_FlagValue& Out) const;

	/** Remove a persistent value for (Key, Scope). @return true if one existed. */
	bool RemovePersistent(const FGameplayTag& Key, const FWorldHub_Scope& Scope);

	/** Drop all persistent state (e.g. on New Game). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Persistent")
	void ClearAll();

	/** Number of persistent slots held. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|Persistent")
	int32 NumPersistent() const { return Persistent.Num(); }

	// ---- World-hub bridge ---------------------------------------------------------------------

	/**
	 * Seed a freshly-initialized world hub from the persistent map (call on level load, AUTHORITY
	 * side). Each persistent slot is written into the hub's registry via its authoritative path so it
	 * mirrors to clients. Null hub is a safe no-op.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Persistent")
	void SeedWorldHub(UWorldHub_StateHubSubsystem* WorldHub) const;

	/**
	 * Receive a world hub's save-bearing slots before level travel (call from the hub's
	 * FlushSaveStateTo, AUTHORITY side). Overwrites matching persistent slots. Null hub is a no-op.
	 */
	void ReceiveFlush(const UWorldHub_StateHubSubsystem* WorldHub);

	/** Receive an already-built snapshot directly (used by ReceiveFlush and by save apply). */
	void ReceiveSnapshot(const FWorldHub_Snapshot& Snapshot);

	/** Build a snapshot of the current persistent map (used by save build). */
	void BuildSnapshot(FWorldHub_Snapshot& Out) const;

	// ---- Save bridge --------------------------------------------------------------------------

	/**
	 * Construct and populate a UWorldHub_SaveGame from the current persistent map. The returned
	 * object is a transient UObject owned by the caller's scope; hand it to SaveAsync/SaveNow.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Save")
	UWorldHub_SaveGame* BuildSaveObject() const;

	/** Replace the persistent map from a loaded UWorldHub_SaveGame. Null save clears nothing. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Save")
	void ApplyFromSave(UWorldHub_SaveGame* SaveObject);

	/**
	 * Build a save object and write it to Slot through the core save subsystem (async). The
	 * OnPersistentSaved multicast fires on completion (game thread).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Save")
	void SaveToSlotAsync(const FString& Slot);

	/**
	 * Load Slot through the core save subsystem (async) and, on success, apply it into the persistent
	 * map. The OnPersistentLoaded multicast fires on completion (game thread).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub|Save")
	void LoadFromSlotAsync(const FString& Slot);

	/** Fired after an async save initiated by SaveToSlotAsync completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|WorldHub|Save")
	FWorldHub_OnPersistentIO OnPersistentSaved;

	/** Fired after an async load initiated by LoadFromSlotAsync completes. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|WorldHub|Save")
	FWorldHub_OnPersistentIO OnPersistentLoaded;

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Convenience alias for the composite persistent-slot key (shared with the flag registry). */
	using FPersistentKey = FWorldHub_SlotAddress;

	/**
	 * (Scope, Key) -> persistent value. A UPROPERTY so FInstancedStruct payloads and any inner
	 * references are GC-visible. Never replicated (game-instance-scoped persistence cache).
	 */
	UPROPERTY()
	TMap<FWorldHub_SlotAddress, FWorldHub_FlagValue> Persistent;
};
