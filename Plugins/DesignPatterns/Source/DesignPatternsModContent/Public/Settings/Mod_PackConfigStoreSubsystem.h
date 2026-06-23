// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Persist/Seam_Persistable.h"
#include "Settings/Mod_PackSettingsSchema.h"   // FMod_PackSettingValue, UMod_PackSettingsSchema
#include "Mod_PackConfigStoreSubsystem.generated.h"

class UDP_MessageBusSubsystem;
class UDP_ServiceLocatorSubsystem;
class UMod_ContentRegistrySubsystem;

/**
 * Message-bus payload broadcast when a pack's player-facing config changes. PII-free, never replicated.
 * Sent as an FInstancedStruct on DP.Bus.Mod.SettingsChanged.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_SettingsChangedEvent
{
	GENERATED_BODY()

	/** The pack whose config changed (child of DP.Mod.Pack). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Settings")
	FGameplayTag PackId;

	/** The field that changed. Invalid when the change is a whole-pack reset. */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Settings")
	FGameplayTag FieldId;

	/** True when the whole pack was reset to defaults (FieldId is then invalid). */
	UPROPERTY(BlueprintReadOnly, Category = "ModContent|Settings")
	bool bWholePackReset = false;
};

/**
 * Inner per-pack value map. A named USTRUCT wrapper so it can live inside a reflected TMap value (UHT
 * does not allow a nested TMap as a TMap value directly).
 */
USTRUCT()
struct DESIGNPATTERNSMODCONTENT_API FMod_PackSettingValueMap
{
	GENERATED_BODY()

	/** Field id -> stored value for one pack. */
	UPROPERTY()
	TMap<FGameplayTag, FMod_PackSettingValue> Values;
};

/**
 * The persisted save record for the whole config store: a flat list of (pack, field, value) triples.
 * Carried via ISeam_Persistable as an FInstancedStruct — NEVER plain-Replicated. PII-free.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSMODCONTENT_API FMod_PackConfigSaveRecord
{
	GENERATED_BODY()

	/** Stored values flattened across all packs. The owning pack id is paired in StoredPackIds[i]. */
	UPROPERTY(SaveGame)
	TArray<FMod_PackSettingValue> StoredValues;

	/** Parallel array to StoredValues: the owning pack id for each stored value. */
	UPROPERTY(SaveGame)
	TArray<FGameplayTag> StoredPackIds;
};

/**
 * GameInstance subsystem holding the PLAYER'S per-pack config, validated against each pack's
 * UMod_PackSettingsSchema. It is the front door for "what value did the player choose for pack P's
 * field F?".
 *
 * PERSISTENCE: implements ISeam_Persistable so its values save/load with the game. Persistence kind is
 * the stable tag DP.Persist.Mod.PackConfig. RestoreState is UNCONDITIONAL — this is LOCAL per-machine
 * config, so an authority gate would wrongly drop a client's chosen options; there is no authoritative
 * state here to guard. Because the core save object does not auto-discover GI subsystems, this subsystem
 * REGISTERS itself with the service locator (under DP.Persist.Mod.PackConfig) so a save object can
 * enumerate and call it; CaptureState/RestoreState remain fully functional regardless.
 *
 * Non-replicated. Broadcasts DP.Bus.Mod.SettingsChanged on every mutation.
 */
UCLASS()
class DESIGNPATTERNSMODCONTENT_API UMod_PackConfigStoreSubsystem
	: public UDP_GameInstanceSubsystem
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * Read the player's value for a pack field. If the player never set it, the schema's default is
	 * synthesised (so callers always get a usable value). Returns false only when no schema for the pack
	 * is resolvable and no stored value exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Settings")
	bool GetSetting(FGameplayTag PackId, FGameplayTag FieldId, FMod_PackSettingValue& Out) const;

	/**
	 * Set the player's value for a pack field. The value is validated/clamped against the pack's schema
	 * (numeric clamp, kind shape) before being stored; an unknown field for a resolvable schema is
	 * rejected (logged). Broadcasts DP.Bus.Mod.SettingsChanged on success.
	 */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Settings")
	bool SetSetting(FGameplayTag PackId, const FMod_PackSettingValue& Value);

	/** Drop all stored values for a pack so subsequent reads return schema defaults. Broadcasts changed. */
	UFUNCTION(BlueprintCallable, Category = "ModContent|Settings")
	void ResetPackToDefaults(FGameplayTag PackId);

	/** True if the player has explicitly stored a value for this pack field. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Settings")
	bool HasStoredValue(FGameplayTag PackId, FGameplayTag FieldId) const;

	/** Every pack id the store currently holds at least one value for. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ModContent|Settings")
	TArray<FGameplayTag> GetConfiguredPacks() const;

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

private:
	/** Resolve the settings schema for a pack via the mod-aware registry (override layer aware). Null when absent. */
	const UMod_PackSettingsSchema* ResolveSchema(FGameplayTag PackId) const;

	/** Clamp/validate a value against a schema field. Returns the sanitised value. */
	static FMod_PackSettingValue SanitiseAgainstField(const FMod_PackSettingField& Field, const FMod_PackSettingValue& In);

	/** Broadcast a settings-changed event (best-effort; no-op if the bus is absent). */
	void BroadcastSettingsChanged(FGameplayTag PackId, FGameplayTag FieldId, bool bWholePackReset) const;

	/** Resolve the message bus (may be null in early/teardown contexts). */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Resolve the service locator (may be null in early/teardown contexts). */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/**
	 * Stored values, keyed by pack id then field id. Plain USTRUCT values held alive by this UPROPERTY
	 * map. Per-machine; rebuilt on RestoreState.
	 */
	UPROPERTY(Transient)
	TMap<FGameplayTag, FMod_PackSettingValueMap> StoredByPack;
};
