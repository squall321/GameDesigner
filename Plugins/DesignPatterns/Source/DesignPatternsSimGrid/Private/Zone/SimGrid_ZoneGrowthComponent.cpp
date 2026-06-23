// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Zone/SimGrid_ZoneGrowthComponent.h"
#include "Zone/SimGrid_ZoneCarrier.h"
#include "Zone/SimGrid_ZoneRuleStrategy.h"
#include "Clock/Seam_SimClock.h"
#include "Grid/Seam_TileProviderRead.h"
#include "SimGrid_NativeTags.h"
#include "Settings/SimGrid_DeveloperSettings.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"
#include "Engine/World.h"

// ── Constructor ──────────────────────────────────────────────────────────────

USimGrid_ZoneGrowthComponent::USimGrid_ZoneGrowthComponent()
{
	// Authority-side fixed-step driver: must tick to advance the accumulator.
	PrimaryComponentTick.bCanEverTick = true;

	// Purely server-side: clients receive growth through zone entry replication, not this component.
	SetIsReplicatedByDefault(false);
}

// ── UActorComponent overrides ─────────────────────────────────────────────────

void USimGrid_ZoneGrowthComponent::BeginPlay()
{
	Super::BeginPlay();

	// Reset the accumulator so any leftover sub-step seconds from a previous BeginPlay (e.g. after a
	// seamless travel) do not award a spurious first growth step.
	SimSecondAccumulator = 0.f;
}

void USimGrid_ZoneGrowthComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// Drop the clock override so GC can collect whatever the override pointed at.
	ClockOverride = TScriptInterface<ISeam_SimClock>();

	Super::EndPlay(EndPlayReason);
}

void USimGrid_ZoneGrowthComponent::TickComponent(
	float DeltaTime,
	ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// AUTHORITY ONLY: clients rely on replicated zone-entry growth, not local tick.
	UWorld* W = GetWorld();
	if (!W || W->GetNetMode() == NM_Client)
	{
		return;
	}

	// ── Advance accumulator via the sim clock ──────────────────────────────────
	// If the clock is present and paused the accumulator is frozen entirely, which ensures a paused
	// game never awards growth steps even if many real-time seconds elapse while paused.
	TScriptInterface<ISeam_SimClock> Clock = ResolveClock();
	if (UObject* ClockObj = Clock.GetObject())
	{
		if (ISeam_SimClock::Execute_IsPaused(ClockObj))
		{
			// Simulation is paused — do not advance, do not step.
			return;
		}

		// Scale real delta by the clock's time multiplier (e.g. 2× fast-forward).
		const float Scale = static_cast<float>(ISeam_SimClock::Execute_GetTimeScale(ClockObj));
		SimSecondAccumulator += DeltaTime * Scale;
	}
	else
	{
		// No clock registered: advance at wall-clock speed as a safe fallback so growth still
		// progresses in test maps / standalone without a clock actor.
		SimSecondAccumulator += DeltaTime;
	}

	// ── Drain the accumulator in fixed-size steps ──────────────────────────────
	// Using a while-loop handles the (rare) case where a single frame covers more than one step
	// (e.g. after a large time-scale spike). SecondsPerSimDay converts accumulated sim-seconds to
	// the day units that strategy rules receive, keeping rules free of the raw time-step size.
	while (SimSecondAccumulator >= StepIntervalSimSeconds)
	{
		SimSecondAccumulator -= StepIntervalSimSeconds;

		const float ElapsedDays = StepIntervalSimSeconds / FMath::Max(0.01f, SecondsPerSimDay);
		RunGrowthStep(ElapsedDays);
	}
}

// ── Public API ────────────────────────────────────────────────────────────────

void USimGrid_ZoneGrowthComponent::StepGrowth(float ElapsedDays)
{
	// AUTHORITY ONLY: same guard as TickComponent — prevents a Blueprint call on a client from
	// writing growth directly and desynchronising replicated state.
	UWorld* W = GetWorld();
	if (!W || W->GetNetMode() == NM_Client)
	{
		return;
	}

	RunGrowthStep(ElapsedDays);
}

// ── Private helpers ───────────────────────────────────────────────────────────

/**
 * Returns the active ISeam_SimClock, honouring the ClockOverride first, then the service locator.
 * If ClockServiceTag is invalid, falls back to the well-known "DP.Service.SimClock" anchor tag.
 * Returns an empty TScriptInterface when no clock is reachable (caller must treat this as wall-time).
 */
TScriptInterface<ISeam_SimClock> USimGrid_ZoneGrowthComponent::ResolveClock() const
{
	// ── 1. ClockOverride (injected via SetClockOverride) ──────────────────────
	if (UObject* OverrideObj = ClockOverride.GetObject())
	{
		// The Transient UPROPERTY prevents GC from collecting it mid-frame, but double-check that the
		// interface is still valid (e.g. if the object was explicitly destroyed).
		if (IsValid(OverrideObj))
		{
			return ClockOverride;
		}
	}

	// ── 2. Service locator ────────────────────────────────────────────────────
	// Use the designer-assigned ClockServiceTag, or fall back to the module-wide well-known key.
	const FGameplayTag Key = ClockServiceTag.IsValid()
		? ClockServiceTag
		: FGameplayTag::RequestGameplayTag(FName("DP.Service.SimClock"));

	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		UE_LOG(LogDP, Verbose,
			TEXT("USimGrid_ZoneGrowthComponent::ResolveClock — no service locator; falling back to wall-time."));
		return TScriptInterface<ISeam_SimClock>();
	}

	UObject* ClockObj = Locator->ResolveService(Key);
	if (!ClockObj || !ClockObj->Implements<USeam_SimClock>())
	{
		UE_LOG(LogDP, Verbose,
			TEXT("USimGrid_ZoneGrowthComponent::ResolveClock — service '%s' not found or not a SimClock; falling back to wall-time."),
			*Key.ToString());
		return TScriptInterface<ISeam_SimClock>();
	}

	TScriptInterface<ISeam_SimClock> Result;
	Result.SetObject(ClockObj);
	Result.SetInterface(Cast<ISeam_SimClock>(ClockObj));
	return Result;
}

/**
 * Returns the ISeam_TileProviderRead grid, mirroring the pattern from SimGrid_AutoTileComponent:
 * prefer a cached weak pointer, then resolve from the service locator (with a DeveloperSettings
 * override key), cache the result, and return empty if the grid is absent.
 *
 * The cache is a TWeakObjectPtr so a level-unload that destroys the provider invalidates the cache
 * automatically, and the next call re-resolves without dangling pointer risk.
 */
TScriptInterface<ISeam_TileProviderRead> USimGrid_ZoneGrowthComponent::ResolveGrid() const
{
	TScriptInterface<ISeam_TileProviderRead> Result;

	// ── 1. Try the cached provider ────────────────────────────────────────────
	if (UObject* Cached = CachedGridObject.Get())
	{
		if (Cached->Implements<USeam_TileProviderRead>())
		{
			Result.SetObject(Cached);
			Result.SetInterface(Cast<ISeam_TileProviderRead>(Cached));
			return Result;
		}
		// Cache is stale (provider was destroyed without unregistering) — reset and re-resolve.
		CachedGridObject.Reset();
	}

	// ── 2. Resolve from the service locator ───────────────────────────────────
	// DeveloperSettings may redirect to a project-specific tag; fall through to the SimGridTags
	// anchor when no override is set.
	FGameplayTag Key = SimGridTags::Service_TileProvider;
	if (const USimGrid_DeveloperSettings* Settings = USimGrid_DeveloperSettings::Get())
	{
		if (Settings->TileProviderServiceTag.IsValid())
		{
			Key = Settings->TileProviderServiceTag;
		}
	}

	const UDP_ServiceLocatorSubsystem* Locator =
		FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this);
	if (!Locator)
	{
		return Result; // empty
	}

	UObject* Provider = Locator->ResolveService(Key);
	if (Provider && Provider->Implements<USeam_TileProviderRead>())
	{
		CachedGridObject = Provider;
		Result.SetObject(Provider);
		Result.SetInterface(Cast<ISeam_TileProviderRead>(Provider));
	}

	return Result;
}

/**
 * Core growth pass: resolves the zone carrier, iterates every painted zone entry, dispatches each
 * to its matching strategy, and routes any growth delta through the carrier's authority mutator.
 *
 * Design notes:
 *  - The carrier's HasAuthority() check is a redundant safety net; the component already guards at
 *    TickComponent/StepGrowth entry. Belt-and-suspenders for direct call sites.
 *  - The grid is resolved once per step and forwarded to every context so strategies can inspect
 *    neighbouring tiles without holding their own grid reference.
 *  - FMath::IsNearlyEqual avoids redundant replication dirty-marks for entries whose growth did not
 *    meaningfully change (float epsilon from the strategy's computation).
 */
void USimGrid_ZoneGrowthComponent::RunGrowthStep(float ElapsedDays)
{
	// ── Resolve the zone carrier ──────────────────────────────────────────────
	ASimGrid_ZoneCarrier* Carrier = ASimGrid_ZoneCarrier::Resolve(this);
	if (!Carrier)
	{
		UE_LOG(LogDP, Verbose,
			TEXT("USimGrid_ZoneGrowthComponent::RunGrowthStep — no zone carrier found; skipping step."));
		return;
	}

	if (!Carrier->HasAuthority())
	{
		// Should not be reachable (we guard at the entry points), but log rather than silently skipping.
		UE_LOG(LogDP, Warning,
			TEXT("USimGrid_ZoneGrowthComponent::RunGrowthStep — carrier lacks authority on this machine; skipping step."));
		return;
	}

	// ── Resolve the optional tile grid ───────────────────────────────────────
	// Rules that only use GrowthLevel/ElapsedDays/Zone work fine with an empty grid (they simply
	// skip neighbour inspection). Resolve once per step, not once per cell.
	const TScriptInterface<ISeam_TileProviderRead> Grid = ResolveGrid();

	// ── Iterate every zoned cell ──────────────────────────────────────────────
	const TArray<FSimGrid_ZoneEntry>& Entries = Carrier->GetZoneEntries();
	for (const FSimGrid_ZoneEntry& Entry : Entries)
	{
		// Find the strategy that applies to this zone type. Skip cells with no governing rule.
		USimGrid_ZoneRuleStrategy* Strategy = FindStrategy(Entry.ZoneTypeTag);
		if (!Strategy)
		{
			continue;
		}

		// Build a self-contained context so the rule needs no engine globals.
		FSimGrid_ZoneGrowthContext Ctx;
		Ctx.Cell           = Entry.Cell;
		Ctx.ZoneTypeTag    = Entry.ZoneTypeTag;
		Ctx.CurrentGrowth  = Entry.GrowthLevel;
		Ctx.ElapsedDays    = ElapsedDays;
		Ctx.Grid           = Grid;
		Ctx.Carrier        = Carrier;

		// Evaluate the new growth level. EvaluateGrowth is a BlueprintNativeEvent on a UObject subclass,
		// so the generated thunk is called directly on the instance (not via Execute_).
		float NewGrowth = Strategy->EvaluateGrowth(Ctx);

		// Clamp to the valid [0,1] range — strategies may compute deltas that overshoot.
		NewGrowth = FMath::Clamp(NewGrowth, 0.f, 1.f);

		// Only write and dirty-mark the fast array if the value actually changed (avoids spurious
		// replication bandwidth when a fully-grown cell's strategy returns 1 every step).
		if (!FMath::IsNearlyEqual(NewGrowth, Entry.GrowthLevel))
		{
			Carrier->SetZoneGrowth(Entry.Cell, NewGrowth);
		}
	}
}

/**
 * Linear search through the Strategies array for the first strategy that governs ZoneType.
 * Strategies are designed to be few (typically 3-10) so linear search is cheaper than a map.
 * Returns null when no strategy matches; the caller skips the cell silently.
 */
USimGrid_ZoneRuleStrategy* USimGrid_ZoneGrowthComponent::FindStrategy(const FGameplayTag& ZoneType) const
{
	for (const TObjectPtr<USimGrid_ZoneRuleStrategy>& Strategy : Strategies)
	{
		if (Strategy && Strategy->AppliesTo(ZoneType))
		{
			return Strategy.Get();
		}
	}
	return nullptr;
}
