// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "GameplayTagContainer.h"
#include "SampleGameInstance.generated.h"

/**
 * Minimal sample GameInstance that demonstrates COMPOSING DesignPatterns plugin modules through their
 * public APIs and seams — without the modules depending on each other. On Init it:
 *   - reads/writes game-wide state through the World hub (DesignPatternsWorld),
 *   - lists existing save slots through the save-slot manager (DesignPatternsSaveSystem), and
 *   - logs a composition summary.
 *
 * This is intentionally code-only and headless-safe so it doubles as a smoke test of the plugin's
 * subsystem wiring. A real game would drive these from gameplay (the app-flow FSM, a HUD, etc.).
 */
UCLASS()
class DESIGNPATTERNSSAMPLE_API USampleGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	//~ Begin UGameInstance
	virtual void Init() override;
	//~ End UGameInstance

	/** Tag the sample uses for a global "tutorial seen" flag in the World hub. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sample")
	FGameplayTag TutorialSeenFlagTag;

	/** Tag the sample uses for a global "enemies defeated" counter in the World hub. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sample")
	FGameplayTag EnemiesDefeatedCounterTag;

private:
	/** Demonstrate the World hub: set a flag, bump a counter, read them back. */
	void DemoWorldHub();

	/** Demonstrate the save-slot manager: list existing slots. */
	void DemoSaveSlots();
};
