// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Flow_FlowTypes.generated.h"

/**
 * Message-bus payload announcing a top-level flow phase change. Flat and net/save-safe (no object
 * refs) so any module can read it off DP.Bus.Flow.PhaseChanged without depending on this module.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEFLOW_API FFlow_PhaseChangedPayload
{
	GENERATED_BODY()

	/** The phase that was just left (invalid on the very first transition from no-phase). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow")
	FGameplayTag PreviousPhase;

	/** The phase that is now active. */
	UPROPERTY(BlueprintReadOnly, Category = "Flow")
	FGameplayTag NewPhase;

	/** True if the transition was forced (bypassed the allowed-transition check). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow")
	bool bForced = false;
};

/**
 * Message-bus payload requesting the UI mediator push or pop a phase screen. The HUD/UI module
 * listens on DP.Bus.Flow.Screen.Push / .Pop and realises it; this module never depends on the UI
 * concrete type. Flat (tags only).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEFLOW_API FFlow_ScreenRequestPayload
{
	GENERATED_BODY()

	/** The screen to push/pop. */
	UPROPERTY(BlueprintReadOnly, Category = "Flow")
	FGameplayTag ScreenTag;

	/** The UI layer the screen lives on. */
	UPROPERTY(BlueprintReadOnly, Category = "Flow")
	FGameplayTag LayerTag;

	/** The phase that owns this screen request (for diagnostics / dedup on the UI side). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow")
	FGameplayTag OwningPhase;
};

/**
 * Message-bus payload carrying loading progress. Broadcast on DP.Bus.Flow.LoadingProgress so a HUD or
 * the loading ViewModel can mirror it. Flat (a normalized progress + a label key).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSGAMEFLOW_API FFlow_LoadingProgressPayload
{
	GENERATED_BODY()

	/** Normalized progress in [0,1]. -1 means "indeterminate" (engine map load gives no fraction). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow")
	float Progress = -1.f;

	/** A status label key/text for the loading UI (e.g. "Loading Level"). */
	UPROPERTY(BlueprintReadOnly, Category = "Flow")
	FText StatusLabel;

	/** The map being loaded (long package name), for diagnostics. */
	UPROPERTY(BlueprintReadOnly, Category = "Flow")
	FString TargetMapName;
};
