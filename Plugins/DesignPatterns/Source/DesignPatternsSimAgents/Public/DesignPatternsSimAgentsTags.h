// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NativeGameplayTags.h"

/**
 * Native (C++-defined) anchor tags for the DesignPatternsSimAgents module.
 *
 * These are ROOT/anchor tags only — concrete activities, locations, needs and service keys are
 * authored by the game project as CHILD tags (in the project's tag config or its own native tags).
 * Anchoring the roots here guarantees the hierarchy exists at startup so tag-hierarchy matching
 * always works (e.g. a schedule whose Activity is "SimAg.Activity.Work.Forge" still matches a brain
 * that keys behaviour off "SimAg.Activity.Work", and a need component advertising
 * "SimAg.Need.Social" answers a brain query for "SimAg.Need").
 *
 * SERVICE KEYS: SimAgents publishes one world-scoped service — the simulation clock — under
 * Service.Clock (a child of DP.Service so the locator's hierarchy matching includes it). Consumers
 * resolve it by that stable tag through UDP_ServiceLocatorSubsystem rather than by concrete type.
 */
namespace SimAgNativeTags
{
	/** Root for sim-agent need identities (SimAg.Need.Social / Fun / Energy / Hygiene...). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Need);

	/** Root for schedule activity identities (SimAg.Activity.Sleep / Work / Eat / Leisure...). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Activity);

	/** Root for schedule location identities (SimAg.Location.Home / Workplace / Tavern...). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Location);

	/** Root for service-locator keys this module registers (children of DP.Service). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service);

	/** Service key for the world simulation clock (USimAg_ClockSubsystem, implements ISeam_SimClock). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_Clock);

	/** Service key for the world job board (USimAg_JobBoardSubsystem, implements ISimAg_JobProvider). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_JobBoard);

	/** Service key for the crowd flow-field provider (USimAg_FlowFieldSubsystem, ISimAg_FlowField). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_FlowField);

	/**
	 * Service key for the world reservation router (USimAg_JobReservationSubsystem, ISeam_JobReservation).
	 * Lets haul / job-chain behaviours lock a single resource without depending on the concrete subsystem.
	 */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Service_JobReservation);

	/** Root for sim-agent memory subject identities (SimAg.Memory.Resource / Threat / Agent...). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Memory);

	/** Root for sim-agent mood/emotion axis identities (SimAg.Mood.Happiness / Stress / Anger...). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Mood);

	/** Root for persistence-kind tags this module's save participants advertise (children of DP.Persist). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist);

	/** Persistence kind advertised by the job board (for ISeam_Persistable record routing). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_JobBoard);

	/** Persistence kind advertised by an agent component (for ISeam_Persistable record routing). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Agent);

	/** Persistence kind advertised by the reservation subsystem (for ISeam_Persistable record routing). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Reservation);

	/** Persistence kind advertised by an agent memory component (for ISeam_Persistable record routing). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Persist_Memory);

	/** Root for message-bus channels broadcast by this module (children of DP.Bus by convention). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus);

	/** Bus channel fired when a need crosses below its critical threshold (payload: FSimAg_NeedEvent). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_NeedCritical);

	/** Bus channel fired when an agent's active schedule activity changes (payload: FSimAg_ActivityEvent). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_ActivityChanged);

	/** Bus channel fired when a job posting changes state (payload: FSimAg_JobEvent). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_JobChanged);

	/** Bus channel fired when an agent's mood axis changes notably (payload: FSimAg_MoodEvent). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_MoodChanged);

	/** Bus channel fired when two agents have a social interaction (payload: FSimAg_SocialEvent). */
	DESIGNPATTERNSSIMAGENTS_API UE_DECLARE_GAMEPLAY_TAG_EXTERN(Bus_SocialInteraction);
}
