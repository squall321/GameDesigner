// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Prog_AchievementDefinition.generated.h"

class UProg_Condition;
class UTexture2D;

/**
 * Data-driven description of one achievement.
 *
 * Identity is the inherited UDP_DataAsset::DataTag (a child of DP.Prog.Achievement authored by the
 * project), so the achievement subsystem and conditions reference achievements by stable tag, never by
 * asset path. Add definitions as content; no code per achievement.
 *
 * The unlock RULE is the Conditions list (Strategy pattern): the achievement unlocks when EVERY
 * condition's Evaluate returns true. An empty list is treated as "never unlocks automatically" (the
 * project must unlock it explicitly), which the editor validation surfaces as a warning.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Achievement Definition"))
class DESIGNPATTERNSPROGRESSION_API UProg_AchievementDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	UProg_AchievementDefinition();

	/** Player-facing title. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement")
	FText Name;

	/** Player-facing description / unlock hint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement", meta = (MultiLine = "true"))
	FText Description;

	/** Optional icon. Soft so the catalog never force-loads textures at startup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement")
	TSoftObjectPtr<UTexture2D> Icon;

	/**
	 * When true the achievement (and its description) is hidden in UI until unlocked. Tracking still
	 * happens normally; only the presentation is gated.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement")
	bool bHidden = false;

	/**
	 * Instanced unlock conditions (Strategy). ALL must pass for the achievement to unlock. Editable
	 * inline so designers compose flag/counter strategies per achievement without subassets.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Achievement")
	TArray<TObjectPtr<UProg_Condition>> Conditions;

	/**
	 * Optional currency granted on unlock (invalid tag = no reward). Granted through ISeam_Wallet on
	 * the unlocking player's wallet, on authority only.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement|Reward")
	FGameplayTag RewardCurrency;

	/** Amount of RewardCurrency to grant on unlock. Ignored if RewardCurrency is invalid. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Achievement|Reward", meta = (ClampMin = "0"))
	int64 RewardAmount = 0;

	/** True if this definition carries a non-zero currency reward. */
	bool HasReward() const { return RewardCurrency.IsValid() && RewardAmount > 0; }

	//~ Begin UDP_DataAsset
	/** All achievement definitions share one asset-manager bucket so the catalog can preload them. */
	virtual FName GetDataAssetType_Implementation() const override;
	//~ End UDP_DataAsset

#if WITH_EDITOR
	//~ Begin UObject (editor validation)
	/** Warns on an empty Conditions list and on a reward currency with a zero amount. */
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
	//~ End UObject
#endif
};
