// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Prog_DeveloperSettings.generated.h"

/**
 * Project-wide tunables for the progression module.
 *
 * These are config-backed designer/engineer knobs (NOT per-actor gameplay numbers — currency values
 * and reward amounts live on data assets / the wallet component). Exposed in Project Settings under
 * "Plugins > DesignPatterns Progression". Every defensive fallback in the module reads from the CDO of
 * this object, which GetDefault() guarantees is non-null; the inline defaults below are the documented
 * shipped values.
 */
UCLASS(config = Game, defaultconfig, meta = (DisplayName = "DesignPatterns Progression"))
class DESIGNPATTERNSPROGRESSION_API UProg_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UProg_DeveloperSettings();

	/** Convenience accessor for the CDO-backed config settings object (never null). */
	static const UProg_DeveloperSettings* Get();

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
	//~ End UDeveloperSettings

	// ---- Wallet ----

	/**
	 * Hard ceiling applied to any single currency balance after an AddCurrency. Prevents a designer
	 * data error or runaway reward loop from overflowing int64 / producing absurd balances. 0 disables
	 * the cap. Spending is never affected.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Wallet", meta = (ClampMin = "0"))
	int64 MaxCurrencyBalance = 1000000000;

	/**
	 * When true the wallet broadcasts a DP.Bus.Prog.BalanceChanged message on every authoritative
	 * balance change (server) and on every replicated change (clients), in addition to the C++/BP
	 * OnBalanceChanged delegate. Lets decoupled listeners (HUD, achievements) react without binding to
	 * the component directly.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Wallet")
	bool bWalletBroadcastsOnBus = true;

	// ---- Achievements ----

	/**
	 * When true the achievement subsystem re-evaluates all not-yet-unlocked achievements whenever ANY
	 * message arrives on DP.Bus.Prog (its own progress/balance traffic) and on the project-configured
	 * extra trigger channels. When false it only evaluates on the explicit trigger channels, to avoid
	 * polling cost in achievement-heavy projects.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Achievements")
	bool bEvaluateOnAllProgEvents = true;

	/**
	 * Additional message-bus channels that should trigger a re-evaluation of achievement conditions
	 * (e.g. a project's DP.Bus.Combat.Killed). The subsystem subscribes to each with child-matching.
	 * Empty by default; the module ships listening to its own DP.Prog bus root only.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Achievements")
	TArray<FGameplayTag> AchievementTriggerChannels;

	/**
	 * Minimum seconds between successive full achievement evaluations, regardless of how many trigger
	 * events arrive. Coalesces bursts (e.g. many kills in one frame) into a single pass. 0 evaluates
	 * on every trigger.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Achievements", meta = (ClampMin = "0.0", Units = "s"))
	float MinEvaluationIntervalSeconds = 0.25f;

	/**
	 * When true the subsystem reports normalized progress to the platform-achievement seam (if bound)
	 * for partially-completed achievements, not only the final unlock. Off by default since not all
	 * platform SDKs support progressive trophies.
	 */
	UPROPERTY(config, EditAnywhere, BlueprintReadOnly, Category = "Achievements")
	bool bReportPlatformProgress = false;
};
