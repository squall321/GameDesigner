// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "SaveX_DeveloperSettings.generated.h"

class UDP_SaveGame;

/** Policy for when a slot's thumbnail (screenshot) capture is requested at save time. */
UENUM(BlueprintType)
enum class ESaveX_ThumbnailPolicy : uint8
{
	/** Never request a thumbnail; the slot UI shows default art. */
	Never,
	/** Request a thumbnail only for explicit, player-initiated named saves (not autosaves). */
	NamedSavesOnly,
	/** Request a thumbnail for every write, including the autosave ring. */
	Always
};

/**
 * Project-wide tunables for the DesignPatterns SaveSystem checkpoint + autosave area.
 *
 * Appears under Project Settings -> Plugins -> Design Patterns Save System. Every gameplay-affecting
 * number the checkpoint component, checkpoint volume and autosave subsystem use is an EditAnywhere/Config
 * field here (no magic numbers in code). The classes that consume this CDO null-check it and fall back to
 * documented defensive defaults so a missing/ misconfigured settings object never crashes the save pipeline.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Save System"))
class DESIGNPATTERNSSAVESYSTEM_API USaveX_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USaveX_DeveloperSettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	// ---- Save object class ----

	/**
	 * Save object class instantiated for checkpoint + autosave writes. A project subclasses UDP_SaveGame
	 * (adding its own gather/scatter UPROPERTYs and OnPreSave/OnPostLoad overrides) and selects it here so
	 * the SaveSystem area never hardcodes a concrete save type. When unset, the area falls back to the base
	 * UDP_SaveGame (which still records the checkpoint transform/id and header metadata).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Save", meta = (MetaClass = "/Script/DesignPatterns.DP_SaveGame"))
	TSoftClassPtr<UDP_SaveGame> SaveGameClass;

	/** When/whether a thumbnail capture is requested at save time (currently advisory for the UI layer). */
	UPROPERTY(EditAnywhere, Config, Category = "Save")
	ESaveX_ThumbnailPolicy ThumbnailPolicy = ESaveX_ThumbnailPolicy::NamedSavesOnly;

	// ---- Slot naming ----

	/**
	 * Reserved slot name the checkpoint feature writes to. A single dedicated slot (overwritten each
	 * checkpoint) so "restore from last checkpoint" is unambiguous and never collides with the autosave ring.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Checkpoint")
	FString CheckpointSlotName = TEXT("DPCheckpoint");

	/**
	 * Prefix for the rotating autosave ring slots. Ring members are named "<Prefix>_<index>" so they are
	 * easy to recognise in a save/load UI and to enumerate by prefix.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Autosave")
	FString AutosaveSlotPrefix = TEXT("DPAutosave");

	// ---- Autosave ring + cadence ----

	/**
	 * Number of slots in the rotating autosave ring. The autosave subsystem cycles writes across this many
	 * slots so an autosave never clobbers the only recent save. Clamped to at least 1.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Autosave", meta = (ClampMin = "1", UIMin = "1", UIMax = "16"))
	int32 AutosaveRingSize = 3;

	/**
	 * Seconds between periodic (timed) autosaves. <= 0 disables the interval driver entirely (autosaves
	 * then happen only on bus events / checkpoints). The driver runs on FTSTicker, removed on Deinitialize.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Autosave", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AutosaveIntervalSeconds = 300.f;

	/**
	 * Minimum seconds that must elapse between any two autosaves regardless of trigger source (timer, bus,
	 * checkpoint). Throttles bursts so several triggers in quick succession collapse to one disk write.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Autosave", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AutosaveMinIntervalSeconds = 30.f;

	/** When true, recording a checkpoint also kicks the autosave ring (a checkpoint is a strong save signal). */
	UPROPERTY(EditAnywhere, Config, Category = "Autosave")
	bool bAutosaveOnCheckpoint = true;

	/**
	 * Bus channels that should trigger an autosave (subject to the throttle). Leave empty to disable
	 * event-driven autosaves. Anchor these under DP.Bus in the project tag table (e.g. DP.Bus.Save.Trigger).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Autosave")
	TArray<FGameplayTag> AutosaveTriggerChannels;

	// ---- Named slots (slot-manager policy) ----

	/**
	 * Maximum number of player-visible NAMED slots (excludes the autosave ring and the reserved
	 * checkpoint/continue slots). The slot manager refuses to create a NEW named slot beyond this
	 * count; overwriting an existing named slot is always allowed. A defensive floor of 1 is applied
	 * at read time so a misconfigured 0 never bricks saving.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Slots", meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxNamedSlots = 16;

	/**
	 * Reserved name for the single "continue" slot the slot manager writes/resolves for a Continue
	 * button. Excluded from the named-slot cap and from autosave-ring enumeration so "continue" is a
	 * stable, unambiguous target distinct from the rotating ring.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Slots")
	FString ContinueSlotName = TEXT("DPContinue");

	// ---- Service registration ----

	/**
	 * Service-locator key under which the slot manager registers its ISeam_SaveSlotManager adapter.
	 * Defaults to SaveXNativeTags::Service_SaveSlotManager (seeded in the ctor); override only if a
	 * project deliberately relocates the key. When invalid, the manager skips locator registration
	 * (and logs) rather than registering under an empty tag.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Services")
	FGameplayTag SlotManagerServiceTag;

	/** Convenience accessor (may return nullptr in unusual early-load / cooker contexts; callers null-check). */
	static const USaveX_DeveloperSettings* Get();

	// ---- Validated accessors (apply defensive floors regardless of config edits) ----

	/** Named-slot cap, floored to >= 1. */
	int32 GetEffectiveMaxNamedSlots() const { return FMath::Max(1, MaxNamedSlots); }

	/** Autosave ring size, floored to >= 1 (the field clamps at 1, but read defensively). */
	int32 GetEffectiveAutosaveRingSize() const { return FMath::Max(1, AutosaveRingSize); }

	/** Minimum seconds between any two autosaves; non-negative. <= 0 means "no throttle". */
	float GetEffectiveAutosaveMinInterval() const { return FMath::Max(0.f, AutosaveMinIntervalSeconds); }

	/** True if a thumbnail should be requested for a write of the given kind. */
	bool ShouldRequestThumbnail(bool bIsAutosave) const
	{
		switch (ThumbnailPolicy)
		{
		case ESaveX_ThumbnailPolicy::Always:         return true;
		case ESaveX_ThumbnailPolicy::NamedSavesOnly: return !bIsAutosave;
		case ESaveX_ThumbnailPolicy::Never:          return false;
		default:                                     return false;
		}
	}
};
