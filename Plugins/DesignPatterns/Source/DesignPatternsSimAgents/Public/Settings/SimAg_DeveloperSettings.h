// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SimAg_DeveloperSettings.generated.h"

class UDP_StrategySelector;

/**
 * Project-wide configuration for the DesignPatternsSimAgents module. Appears under
 * Project Settings → Plugins → Design Patterns Sim Agents. Editing here requires no code.
 *
 * These are the genre-neutral tunables shared across the sim-agent systems: clock cadence,
 * default brain (utility-AI selector) class, calendar shape, needs replication throttle, and the
 * job-board / claim parameters consumed by sibling areas of this module. All values are exposed
 * via UPROPERTY(EditAnywhere, Config) — there are no hardcoded magic gameplay numbers in code.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Design Patterns Sim Agents"))
class DESIGNPATTERNSSIMAGENTS_API USimAg_DeveloperSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USimAg_DeveloperSettings();

	//~ Begin UDeveloperSettings
	/** Group under the "Plugins" category in Project Settings. */
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	//~ End UDeveloperSettings

	/**
	 * How many times per second an agent brain re-evaluates its utility selector. This is the
	 * decision cadence, NOT the locomotion/physics tick — keeping it low (a few Hz) is what lets
	 * a large crowd of agents stay cheap. Clamped to a sane positive range.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Brain", meta = (ClampMin = "0.1", ClampMax = "30.0", UIMin = "0.5", UIMax = "10.0"))
	float DecisionTickHz = 2.f;

	/**
	 * Default utility-AI brain (core strategy selector) used by agents that don't specify their own.
	 * Soft so the class only loads when an agent that uses the default is actually spawned.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Brain")
	TSoftClassPtr<UDP_StrategySelector> DefaultBrainClass;

	/**
	 * Hours in one in-sim day. Drives the simulation clock calendar and the schedule hour math.
	 * 24 is the natural default; smaller values give faster, more readable day/night cycles.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Clock", meta = (ClampMin = "1", ClampMax = "240"))
	int32 DefaultHoursPerDay = 24;

	/**
	 * Real-world seconds between authoritative needs replication flushes. Needs drain every frame on
	 * the server but only the throttled snapshot crosses the wire, so a crowd of agents costs a
	 * bounded, tunable amount of bandwidth rather than per-frame meter spam.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Needs", meta = (ClampMin = "0.05", ClampMax = "10.0", UIMin = "0.1", UIMax = "5.0"))
	float NeedsReplicationCadence = 0.5f;

	/**
	 * Maximum number of job-board postings an agent considers as "relevant" candidates per decision
	 * pass. Caps the scoring cost of job selection on crowded boards. Consumed by the brain/job-board
	 * sibling area of this module.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Jobs", meta = (ClampMin = "1", ClampMax = "256"))
	int32 JobBoardRelevancyCap = 16;

	/**
	 * Real-world seconds an agent waits between attempts to (re)claim a contested job/resource. A
	 * non-zero interval prevents claim thrash when many agents target the same posting. Consumed by
	 * the brain/job-board sibling area of this module.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Jobs", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float ClaimInterval = 1.f;

	/**
	 * Default neighbour-search radius (world units) used by crowd separation when a steering component
	 * does not specify its own. Bounds how far the flow-field fallback looks for nearby agents to push
	 * away from. Consumed by the crowd steering / flow-field sibling area of this module.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Crowd", meta = (ClampMin = "1.0", ClampMax = "5000.0", UIMin = "50.0", UIMax = "1000.0"))
	float DefaultSeparationRadius = 150.f;

	/**
	 * Relative weight of the crowd-separation push versus the goal-seeking flow direction when the
	 * steering component blends them into a desired velocity. 0 = ignore separation, 1 = equal weight.
	 * A pure designer weight (not a hardcoded magic number in steering code).
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Crowd", meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "2.0"))
	float SeparationWeight = 0.6f;

	/**
	 * How many times per second a steering component recomputes its desired velocity. Like the brain
	 * cadence this is deliberately low for crowds; locomotion itself still moves every frame.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Crowd", meta = (ClampMin = "1.0", ClampMax = "60.0", UIMin = "5.0", UIMax = "30.0"))
	float SteeringTickHz = 10.f;

	/**
	 * Distance (world units) within which the steering component considers itself "arrived" at the move
	 * target and stops issuing movement input. Avoids jitter circling the goal.
	 */
	UPROPERTY(EditAnywhere, Config, Category = "Crowd", meta = (ClampMin = "1.0", ClampMax = "1000.0", UIMin = "10.0", UIMax = "300.0"))
	float ArrivalRadius = 60.f;

	/** Convenience accessor (never null in a running game; the CDO is used for config). */
	static const USimAg_DeveloperSettings* Get();
};
