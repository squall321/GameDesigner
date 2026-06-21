// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Skill_DeveloperSettings.generated.h"

/**
 * Project-wide configuration for the DesignPatternsSkillTree module. Appears under
 * Project Settings → Plugins → Design Patterns Skill Tree. Editing here requires no code.
 *
 * These are the genre-neutral tunables and fallbacks for the skill system: the save-slot prefix the
 * save participant uses, the default point channel/currency used when authored content leaves them
 * unset, and the absolute rank/point safety caps the progression component clamps against. The
 * consumers read these from the CDO; when the CDO is somehow null they fall back to the documented
 * inline defensive defaults baked next to each consumer. No magic gameplay numbers live in the
 * subsystem/component logic — everything tunable lives here.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Skill Tree"))
class DESIGNPATTERNSSKILLTREE_API USkill_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USkill_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Group under the "Plugins" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	//~ End UDeveloperSettings

	// ---- Persistence ----

	/**
	 * Prefix prepended to the per-owner save-slot name the save participant uses when wrapping the core
	 * UDP_SaveGameSubsystem (final slot is e.g. "SkillTree_<OwnerKey>"). Keeps skill saves from colliding
	 * with other systems' slots. Authored, never hardcoded in the save area.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Persistence")
	FString SaveSlotPrefix = TEXT("SkillTree_");

	/**
	 * Whether the save participant auto-saves progression after every successful learn/respec (vs. only on
	 * an explicit project save). Off by default so projects control their own save cadence.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Persistence")
	bool bAutoSaveOnChange = false;

	// ---- Default vocabulary (used when authored content leaves a field unset) ----

	/**
	 * Point channel used when a consumer asks the point source for points without naming a channel, or when
	 * a tree/node leaves its channel unset. A project authors its real channels under Skill.Channel.*; this
	 * is the safety-net default so the system works before content sets one.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (Categories = "Skill.Channel"))
	FGameplayTag DefaultPointChannel;

	/**
	 * Wallet currency tag used for a node whose CostCurrency is unset but whose tree's respec policy is
	 * Cost (so a respec still has something to charge). Leave unset to mean "respec costs only points".
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Defaults", meta = (Categories = "Economy.Currency"))
	FGameplayTag DefaultRespecCurrency;

	// ---- Safety caps (the progression component clamps against these) ----

	/**
	 * Absolute ceiling on any node's effective MaxRank, regardless of what an asset authors. Guards a
	 * mis-authored asset from letting a node be learned an unbounded number of times.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Limits", meta = (ClampMin = "1", ClampMax = "100", UIMin = "1", UIMax = "20"))
	int32 AbsoluteMaxRank = 10;

	/**
	 * Absolute ceiling on the available-point pool the component will track for one owner/channel. A
	 * defensive bound against a misbehaving ISkill_PointSource reporting an absurd earned total.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Limits", meta = (ClampMin = "0", ClampMax = "100000", UIMin = "0", UIMax = "1000"))
	int32 MaxTrackedPoints = 1000;

	/** Convenience accessor (never null in a running game; the CDO carries the config). */
	static const USkill_DeveloperSettings* Get();

	/** The configured save-slot prefix, falling back to the inline default if the CDO is unavailable. */
	static FString GetSaveSlotPrefixSafe();
};
