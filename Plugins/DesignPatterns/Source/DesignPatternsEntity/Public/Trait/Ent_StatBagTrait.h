// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Trait/Ent_Trait.h"
#include "Needs/Seam_NeedProvider.h"
#include "Ent_StatBagTrait.generated.h"

/**
 * One authored stat definition: a tag-keyed float with optional clamping range.
 *
 * The range is used both to clamp writes and to compute a normalized [0,1] value for the
 * ISeam_NeedProvider view (so a stat can double as a need such as hunger/energy). bClamp / Min / Max
 * are tunables authored per stat — no hardcoded gameplay numbers.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_StatDef
{
	GENERATED_BODY()

	/** Stat identity (e.g. Ent.Stat.Health, or a need tag the agent brain reads). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Entity|Stat")
	FGameplayTag StatTag;

	/** Starting / authored value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Entity|Stat")
	float Value = 0.f;

	/** When true, writes clamp into [MinValue, MaxValue] and the normalized view is well-defined. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Entity|Stat")
	bool bClamp = false;

	/** Lower clamp bound (used only when bClamp). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Entity|Stat", meta = (EditCondition = "bClamp"))
	float MinValue = 0.f;

	/** Upper clamp bound (used only when bClamp). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Entity|Stat", meta = (EditCondition = "bClamp"))
	float MaxValue = 1.f;
};

/**
 * Durable save record for UEnt_StatBagTrait — the live tag->float values.
 *
 * Carried inside an FInstancedStruct, never a plain replicated UPROPERTY.
 */
USTRUCT()
struct DESIGNPATTERNSENTITY_API FEnt_StatBagTraitSave
{
	GENERATED_BODY()

	/** Snapshot of the live values keyed by stat tag. */
	UPROPERTY()
	TMap<FGameplayTag, float> Values;
};

/**
 * Concrete data trait holding a tag->float bag of stats on the entity.
 *
 * Seeded from the authored InitialStats (capturing per-stat clamp ranges), then mutated at runtime via
 * Get/Set/Add. Because stats are tag-keyed normalized-capable floats, this trait also implements the
 * shared ISeam_NeedProvider so a clamped stat can serve directly as a need (hunger, energy, ...) that an
 * AI brain composes alongside other providers — no separate needs component required.
 *
 * State is single-machine (subobject of a non-replicated component) and persisted via SaveState/RestoreState.
 */
UCLASS(Blueprintable, EditInlineNew, DefaultToInstanced, meta = (DisplayName = "Entity Stat-Bag Trait"))
class DESIGNPATTERNSENTITY_API UEnt_StatBagTrait : public UEnt_Trait, public ISeam_NeedProvider
{
	GENERATED_BODY()

public:
	UEnt_StatBagTrait();

	/** Authored stats used to seed the live bag (and their clamp ranges) when the trait is added. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|Entity|StatBag")
	TArray<FEnt_StatDef> InitialStats;

	/** Current value of StatTag, or DefaultValue if the stat is unknown. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Entity|StatBag")
	float GetStat(FGameplayTag StatTag, float DefaultValue = 0.f) const;

	/** True if StatTag exists in the bag (regardless of value). OutValue receives the value when found. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|StatBag")
	bool TryGetStat(FGameplayTag StatTag, float& OutValue) const;

	/**
	 * Set StatTag to NewValue (clamped to the stat's range if it was authored with bClamp). Creates the
	 * stat unclamped if it did not exist. Returns the stored value.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|StatBag")
	float SetStat(FGameplayTag StatTag, float NewValue);

	/** Add Delta to StatTag (clamped if applicable). Creates the stat at Delta if it did not exist. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|StatBag")
	float AddToStat(FGameplayTag StatTag, float Delta);

	/** Normalized [0,1] value of StatTag using its clamp range; 0 if absent or unclamped/degenerate range. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Entity|StatBag")
	float GetStatNormalized(FGameplayTag StatTag) const;

	/** Append every stat tag currently in the bag. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Entity|StatBag")
	void GetStatTags(FGameplayTagContainer& OutTags) const;

	//~ Begin UEnt_Trait.
	virtual void OnTraitAdded_Implementation(UEnt_EntityComponent* OwningComponent_In) override;
	virtual void SaveState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	//~ End UEnt_Trait.

	//~ Begin ISeam_NeedProvider — a clamped stat IS a need (normalized satisfaction).
	virtual float GetNeedNormalized_Implementation(FGameplayTag NeedTag) const override;
	virtual bool SupportsNeed_Implementation(FGameplayTag NeedTag) const override;
	virtual void GetSupportedNeeds_Implementation(FGameplayTagContainer& OutNeeds) const override;
	//~ End ISeam_NeedProvider.

protected:
	/** Authored clamp ranges by stat tag, captured from InitialStats so runtime writes clamp correctly. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, FEnt_StatDef> StatDefs;

	/** The runtime-mutable values. Transient; persistence goes through SaveState/RestoreState. */
	UPROPERTY(Transient)
	TMap<FGameplayTag, float> LiveValues;

	/** Clamp Value into StatTag's authored range if it was declared with bClamp; otherwise unchanged. */
	float ClampForStat(FGameplayTag StatTag, float Value) const;
};
