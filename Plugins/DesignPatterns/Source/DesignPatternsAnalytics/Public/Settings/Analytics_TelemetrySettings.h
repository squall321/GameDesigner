// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "GameplayTagContainer.h"
#include "Analytics_TelemetrySettings.generated.h"

class UAnalytics_TelemetryDataAsset;

/**
 * Project configuration for the deepening telemetry area of DesignPatternsAnalytics. Appears under
 * Project Settings -> Plugins -> Design Patterns Telemetry. SEPARATE from the shipped
 * UAnalytics_DeveloperSettings (which is left untouched): this only adds the new knobs the
 * funnel/heatmap/economy/performance/breadcrumb/dashboard subsystems read.
 *
 * Everything here is per-machine/local. Nothing replicates or saves into gameplay save state. All
 * tunable numbers live in the referenced UAnalytics_TelemetryDataAsset (no magic numbers in code);
 * this asset holds only enable flags, service-locator keys and the data-asset / bus-channel
 * selection.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Telemetry"))
class DESIGNPATTERNSANALYTICS_API UAnalytics_TelemetrySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UAnalytics_TelemetrySettings();

	virtual FName GetCategoryName() const override { return FName("Plugins"); }

	/** Convenience CDO accessor. Never null after engine init; callers still null-check defensively. */
	static const UAnalytics_TelemetrySettings* Get();

	// ---- Tunable data ----

	/**
	 * Identity tag of the UAnalytics_TelemetryDataAsset that supplies every tunable number for the
	 * telemetry subsystems. Resolved through the data registry. When unset/unresolved each subsystem
	 * applies the data asset's CDO defaults (a documented inert fallback, never a crash).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Data")
	FGameplayTag TelemetryDataTag;

	// ---- Per-area enable flags (all OFF-by-default-safe where they have shipping cost) ----

	/** Master switch for the GI funnel/cohort subsystem. */
	UPROPERTY(EditAnywhere, Config, Category = "Enable")
	bool bEnableFunnel = true;

	/** Master switch for the world heatmap subsystem. */
	UPROPERTY(EditAnywhere, Config, Category = "Enable")
	bool bEnableHeatmap = true;

	/** Master switch for the performance subsystem's sampling ticker. */
	UPROPERTY(EditAnywhere, Config, Category = "Enable")
	bool bEnablePerformance = true;

	/** Master switch for the crash/error breadcrumb subsystem. */
	UPROPERTY(EditAnywhere, Config, Category = "Enable")
	bool bEnableBreadcrumb = true;

	/**
	 * Master switch for the live debug dashboard. OFF by default in shipping posture: the dashboard
	 * is a QA tool and should not aggregate in a shipped build unless a project opts in.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Enable")
	bool bEnableDebugDashboard = false;

	// ---- Bus channels observed (breadcrumb + dashboard) ----

	/**
	 * Bus subtree the breadcrumb subsystem subscribes to so bus activity automatically leaves
	 * crumbs. Seeded from AnalyticsTelemetryTags::DefaultTelemetryBusChannel; a project narrows it
	 * to scope what is auto-recorded. Invalid disables the auto-crumb bridge (manual Leave still works).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Bus")
	FGameplayTag BreadcrumbBusChannelToObserve;

	/**
	 * Bus subtree the debug dashboard subscribes to for live aggregation. Seeded from the same
	 * default. Invalid disables the dashboard's bus bridge (it then only reflects OnEventRecorded).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Bus")
	FGameplayTag DashboardBusChannelToObserve;

	// ---- Service keys (optional host-supplied seams) ----

	/**
	 * Service-locator tag under which the host may register an ISeam_Wallet adapter for the economy
	 * telemetry component to auto-sample balances. When unset, the component records only explicit
	 * RecordResourceFlow calls (a documented inert fallback).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Services")
	FGameplayTag WalletServiceTag;

	// ---- Convenience ----

	/** True if any telemetry area is enabled (cheap gate a host can read before wiring). */
	bool IsAnyTelemetryEnabled() const
	{
		return bEnableFunnel || bEnableHeatmap || bEnablePerformance || bEnableBreadcrumb || bEnableDebugDashboard;
	}
};
