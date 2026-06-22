// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Data/DPDataAsset.h"
#include "GameplayTagContainer.h"
#include "Flow_BootStepDefinition.generated.h"

/**
 * One ordered step in the data-driven boot sequence. All timings, screens and preload sets are designer
 * data — there are NO magic boot constants in code. The boot controller runs a built-in side effect per
 * StepKind (legal screen, preload, profile load, first-run) and treats an unknown kind as a pure
 * timed/preload step.
 *
 * Identity is the inherited DataTag (a unique per-step tag); StepKind selects the behaviour.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Flow Boot Step Definition"))
class DESIGNPATTERNSGAMEFLOW_API UFlow_BootStepDefinition : public UDP_DataAsset
{
	GENERATED_BODY()

public:
	/**
	 * What kind of boot step this is (Flow.BootStep.Legal / Preload / ProfileLoad / FirstRun, or a
	 * project-authored kind). The controller maps known kinds to a built-in side effect.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (Categories = "Flow.BootStep"))
	FGameplayTag StepKind;

	/**
	 * Minimum seconds this step stays active even if its work finishes sooner (e.g. a logo must show for
	 * at least N seconds). 0 = no minimum. Designer data; the controller never hard-codes a duration.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (ClampMin = "0.0"))
	float MinSeconds = 0.f;

	/**
	 * Optional maximum seconds before the step is force-completed even if its work has not signalled done
	 * (a defensive timeout so a stuck preload/profile-load can never wedge boot forever). 0 = no timeout.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (ClampMin = "0.0"))
	float TimeoutSeconds = 0.f;

	/** Optional screen tag pushed onto the UI mediator while this step runs (empty = no screen). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (Categories = "DP.UI.Screen"))
	FGameplayTag ScreenTag;

	/** Optional UI layer for the step screen (empty falls back to the Flow settings default layer). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (Categories = "DP.UI.Layer"))
	FGameplayTag LayerTag;

	/**
	 * Soft asset paths to front-load through the loading screen during this step (used by Preload steps,
	 * but any step may carry preloads). The controller drives a real progress fraction over these.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot", meta = (AllowedClasses = "/Script/Engine.Object"))
	TArray<FSoftObjectPath> Preload;

	/**
	 * When true, boot does NOT advance past this step until its work signals complete (preload finished /
	 * profile loaded). When false the step is purely time-boxed by MinSeconds and advances regardless.
	 * Defensive default true so a gating step (profile load) actually gates.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot")
	bool bGatesOnComplete = true;

	/** When true this step runs ONLY on the first launch (detected via the boot controller's config flag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot")
	bool bFirstRunOnly = false;

	/** Optional status label surfaced to the loading ViewModel while this step runs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Boot")
	FText StatusLabel;
};
