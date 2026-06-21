// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Stats/Seam_StatModifierSink.h"
#include "RPG_StatsComponent.generated.h"

class UCurveFloat;
class URPG_StatsComponent;

/** How a modifier combines with the base value when computing a derived stat. */
UENUM(BlueprintType)
enum class ERPG_StatModOp : uint8
{
	/** Added to the running sum before multipliers (base + sum(Additive)). */
	Additive,
	/** Multiplies the additive-adjusted value: result *= (1 + Magnitude). */
	Multiplicative,
	/** Overrides the final value outright (last override wins). */
	Override
};

/**
 * A single modifier applied to one tag-keyed attribute.
 *
 * Modifiers are sourced (by SourceTag) so they can be removed as a group, e.g. all modifiers
 * granted by a particular equipped item or buff are removed together on unequip/expire.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSRPG_API FRPG_StatModifier
{
	GENERATED_BODY()

	/** Which attribute this modifier affects (e.g. "RPG.Attribute.Strength"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats")
	FGameplayTag AttributeTag;

	/** Combination operation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats")
	ERPG_StatModOp Op = ERPG_StatModOp::Additive;

	/** Magnitude (interpretation depends on Op). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats")
	float Magnitude = 0.f;

	/** Grouping source so a whole batch of modifiers can be removed at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats")
	FGameplayTag SourceTag;

	FRPG_StatModifier() = default;
};

/** Broadcast when a derived attribute value changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FRPG_OnStatChanged, URPG_StatsComponent*, Stats, FGameplayTag, AttributeTag, float, NewValue);
/** Broadcast (server then replicated to clients) when level increases. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FRPG_OnLevelUp, URPG_StatsComponent*, Stats, int32, NewLevel);

/**
 * Tag-keyed attribute set with stacking modifiers, derived stats and a level/XP track.
 *
 * Base attribute values are authored per actor; derived values fold in active
 * FRPG_StatModifiers (additive, then multiplicative, then override). Level and XP are the
 * server-authoritative, replicated state — AddXP/SetLevel are authority-only; clients learn
 * the new level/XP via replication and OnLevelUp via OnRep. Modifiers are NOT replicated:
 * they are derived from replicated sources (equipment/buffs) and recomputed locally, so each
 * machine arrives at the same derived value without extra bandwidth.
 *
 * The XP-to-next-level curve is supplied by an optional UCurveFloat (X = level, Y = XP
 * required to advance FROM that level); when absent a quadratic fallback is used.
 */
UCLASS(ClassGroup = (DesignPatternsRPG), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSRPG_API URPG_StatsComponent : public UActorComponent, public ISeam_StatModifierSink
{
	GENERATED_BODY()

public:
	URPG_StatsComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End UActorComponent

	//~ Begin ISeam_StatModifierSink
	/**
	 * AUTHORITY-ONLY: grant a batch of gameplay-derived buff modifiers under SourceTag. Backed by the same
	 * authority-only Modifiers list that AddModifier/RemoveModifiersFromSource use.
	 */
	virtual void AddModifierBatch_Implementation(FGameplayTag SourceTag, const TArray<FSeam_StatMod>& Mods) override;
	/** AUTHORITY-ONLY: remove the authority-granted batch under SourceTag. */
	virtual void RemoveModifiersFromSource_Implementation(FGameplayTag SourceTag) override;
	/**
	 * LOCAL-DERIVED, NO authority guard: replace the derived modifier group for SourceTag (equipment/affix/
	 * set/encumbrance/status). Stored in a separate DerivedModifiers list that folds into derived stats on
	 * BOTH server and clients, so equipment/encumbrance contributions never desync.
	 */
	virtual void SetDerivedModifierGroup_Implementation(FGameplayTag SourceTag, const TArray<FSeam_StatMod>& Mods) override;
	//~ End ISeam_StatModifierSink

	/** Set the base (pre-modifier) value of an attribute. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Stats")
	void SetBaseAttribute(FGameplayTag AttributeTag, float Value);

	/** Base (pre-modifier) value of an attribute; 0 if unset. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Stats")
	float GetBaseAttribute(FGameplayTag AttributeTag) const;

	/** Derived value of an attribute: base folded with all active modifiers. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Stats")
	float GetAttributeValue(FGameplayTag AttributeTag) const;

	/**
	 * Add a modifier. AUTHORITY ONLY: although the modifier list is not replicated as array data,
	 * modifiers drive derived stats used in gameplay, so they must be granted server-side. Recomputes
	 * and notifies for the affected attribute.
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Stats")
	void AddModifier(const FRPG_StatModifier& Modifier);

	// NOTE: RemoveModifiersFromSource(FGameplayTag) is provided by the implemented ISeam_StatModifierSink
	// (a BlueprintNativeEvent of the same name and signature). Its authority-only removal logic lives in
	// RemoveModifiersFromSource_Implementation below, preserving the original callable signature.

	/** Current character level (replicated). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Stats")
	int32 GetLevel() const { return Level; }

	/** Accumulated XP within the current level (replicated). */
	UFUNCTION(BlueprintCallable, Category = "RPG|Stats")
	float GetCurrentXP() const { return CurrentXP; }

	/** XP required to advance from the given level to the next. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Stats")
	float GetXPToNextLevel(int32 ForLevel) const;

	/**
	 * Grant XP, rolling over into level-ups while enough XP accumulates. AUTHORITY ONLY.
	 * Fires OnLevelUp per level gained (server + clients via OnRep).
	 */
	UFUNCTION(BlueprintCallable, Category = "RPG|Stats")
	void AddXP(float Amount);

	/** Force a level (clamped to >= 1), resetting in-level XP to 0. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "RPG|Stats")
	void SetLevel(int32 NewLevel);

	/** Curve mapping level -> XP required to advance from it. X = level, Y = XP. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RPG|Stats")
	TObjectPtr<UCurveFloat> XPCurve;

	/** Broadcast whenever a derived attribute changes. */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Stats")
	FRPG_OnStatChanged OnStatChanged;

	/** Broadcast whenever level increases (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "RPG|Stats")
	FRPG_OnLevelUp OnLevelUp;

protected:
	/** OnRep for replicated level: fires OnLevelUp on clients when the level climbs. */
	UFUNCTION()
	void OnRep_Level(int32 OldLevel);

	/** OnRep for replicated XP (no side effects beyond UI refresh hooks). */
	UFUNCTION()
	void OnRep_CurrentXP();

private:
	/** Server-authoritative base attribute values, keyed by attribute tag. */
	UPROPERTY()
	TMap<FGameplayTag, float> BaseAttributes;

	/** Authority-granted modifiers (gameplay buffs). Granted server-side; not replicated as array data. */
	UPROPERTY()
	TArray<FRPG_StatModifier> Modifiers;

	/**
	 * LOCALLY-DERIVED modifier groups (equipment affixes, set bonuses, encumbrance, status effects),
	 * recomputed from already-replicated state on BOTH server and clients via the seam's
	 * SetDerivedModifierGroup path. Kept separate from Modifiers so the authority guard on the gameplay-buff
	 * path never strips equipment/encumbrance contributions on clients. Not replicated (each machine derives
	 * the same groups). Keyed by SourceTag so a whole group is replaced atomically.
	 */
	UPROPERTY()
	TArray<FRPG_StatModifier> DerivedModifiers;

	/** Replicated character level. */
	UPROPERTY(ReplicatedUsing = OnRep_Level)
	int32 Level = 1;

	/** Replicated in-level XP. */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentXP)
	float CurrentXP = 0.f;

	/** Compute the derived value of an attribute from base + modifiers. */
	float ComputeDerived(const FGameplayTag& AttributeTag) const;

	/** Recompute and broadcast OnStatChanged for one attribute. */
	void NotifyAttributeChanged(const FGameplayTag& AttributeTag);
};
