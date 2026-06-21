// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"
#include "Seam_ScoreSource.generated.h"

/** One scoreboard row, flat and net/save-safe (no object refs). */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSEAMS_API FSeam_ScoreRow
{
	GENERATED_BODY()

	/** Who/what this row is for (a team tag, a player id projected to a tag, or a category). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Score")
	FGameplayTag Key;

	/** Display label for the row. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Score")
	FText DisplayName;

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|Seam|Score")
	int64 Score = 0;
};

UINTERFACE(BlueprintType, MinimalAPI)
class USeam_ScoreSource : public UInterface
{
	GENERATED_BODY()
};

/**
 * Read seam for match scores. Implemented by a GameState-owned replicated carrier so scores read
 * correctly on clients. The game-flow results screen, HUD, win-conditions and analytics read scores
 * through this without depending on the GameMode module. Writing scores is authority-only and lives on
 * the concrete score subsystem.
 */
class DESIGNPATTERNSSEAMS_API ISeam_ScoreSource
{
	GENERATED_BODY()

public:
	/** Score for a key (0 if absent). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Score")
	int64 GetScore(FGameplayTag Key) const;

	/** All scoreboard rows (for a results/scoreboard UI). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Score")
	void GetAllScores(TArray<FSeam_ScoreRow>& OutRows) const;

	/** True once the match is over and the results are final (safe to show a results screen). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "DesignPatterns|Seam|Score")
	bool AreResultsFinal() const;
};
