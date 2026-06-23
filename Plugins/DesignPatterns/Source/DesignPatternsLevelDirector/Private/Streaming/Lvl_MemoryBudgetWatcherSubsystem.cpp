// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Streaming/Lvl_MemoryBudgetWatcherSubsystem.h"
#include "Streaming/Lvl_StreamingProfileDataAsset.h"

#include "Core/DPLog.h"

#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "HAL/PlatformMemory.h"

namespace
{
	/** Defensive fallbacks used ONLY when the profile asset is unset (never gameplay tunables). */
	constexpr float GFallbackBudgetMB = 4096.f;
	constexpr float GFallbackInterval = 0.5f;
	constexpr float GFallbackMBPerLevel = 32.f;
	constexpr float GFallbackSaturationOvershoot = 0.25f;
	constexpr float GMinInterval = 0.05f;

	/** Bytes -> MB. */
	constexpr double GBytesPerMB = 1024.0 * 1024.0;
}

void ULvl_MemoryBudgetWatcherSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	TimeSinceLastEvaluation = 0.f;
	GlobalPressure = 0.f;
	LastEstimatedResidentMB = 0.f;

	// Capture a baseline physical-memory used (MB) so EstimateResidentMB can use a delta when exposed.
	const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
	if (Stats.UsedPhysical > 0)
	{
		BaselinePhysicalMB = static_cast<float>(static_cast<double>(Stats.UsedPhysical) / GBytesPerMB);
		bHasBaseline = true;
	}

	TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &ULvl_MemoryBudgetWatcherSubsystem::Tick));

	UE_LOG(LogDP, Log, TEXT("[LevelDirector] Memory-budget watcher initialized (baseline=%s)."),
		bHasBaseline ? *FString::Printf(TEXT("%.0fMB"), BaselinePhysicalMB) : TEXT("n/a"));
}

void ULvl_MemoryBudgetWatcherSubsystem::Deinitialize()
{
	if (TickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);
		TickerHandle.Reset();
	}
	Super::Deinitialize();
}

float ULvl_MemoryBudgetWatcherSubsystem::GetEffectiveBudgetMB() const
{
	return Profile ? Profile->GetEffectiveBudgetMB() : GFallbackBudgetMB;
}

float ULvl_MemoryBudgetWatcherSubsystem::GetEffectiveInterval() const
{
	const float Interval = Profile ? Profile->GetEffectiveEvaluationInterval() : GFallbackInterval;
	return FMath::Max(GMinInterval, Interval);
}

bool ULvl_MemoryBudgetWatcherSubsystem::Tick(float DeltaTime)
{
	TimeSinceLastEvaluation += DeltaTime;
	if (TimeSinceLastEvaluation >= GetEffectiveInterval())
	{
		TimeSinceLastEvaluation = 0.f;
		EvaluateBudget();
	}
	return true; // keep ticking
}

float ULvl_MemoryBudgetWatcherSubsystem::EstimateResidentMB() const
{
	// Preferred: the delta of used-physical memory since the baseline (a coarse but real proxy for how
	// much content this world has streamed in). Defensive: never negative.
	if (bHasBaseline)
	{
		const FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
		if (Stats.UsedPhysical > 0)
		{
			const float NowMB = static_cast<float>(static_cast<double>(Stats.UsedPhysical) / GBytesPerMB);
			return FMath::Max(0.f, NowMB - BaselinePhysicalMB);
		}
	}

	// Fallback: count resident streaming levels times the data-authored per-level proxy.
	const float PerLevel = Profile ? Profile->GetEstimatedMBPerResidentLevel() : GFallbackMBPerLevel;
	int32 Resident = 0;
	if (const UWorld* World = GetWorld())
	{
		for (const ULevelStreaming* Level : World->GetStreamingLevels())
		{
			if (Level && Level->IsLevelLoaded())
			{
				++Resident;
			}
		}
	}
	return Resident * PerLevel;
}

void ULvl_MemoryBudgetWatcherSubsystem::EvaluateBudget()
{
	LastEstimatedResidentMB = EstimateResidentMB();

	const float Budget = GetEffectiveBudgetMB();
	const float Saturation = Profile
		? FMath::Max(0.01f, Profile->PressureSaturationOvershoot)
		: GFallbackSaturationOvershoot;

	if (LastEstimatedResidentMB <= Budget || Budget <= 0.f)
	{
		GlobalPressure = 0.f;
		return;
	}

	// Overshoot fraction over budget, mapped to [0,1] by the saturation overshoot.
	const float OvershootFraction = (LastEstimatedResidentMB - Budget) / Budget;
	GlobalPressure = FMath::Clamp(OvershootFraction / Saturation, 0.f, 1.f);
}

float ULvl_MemoryBudgetWatcherSubsystem::GetCategoryPressure(FGameplayTag Category) const
{
	if (!Category.IsValid() || !Profile)
	{
		return GlobalPressure;
	}

	// Lower-priority categories feel pressure SOONER (and harder). Normalize priority into a [0,1]
	// "resistance" where a higher priority resists more of the global pressure.
	const float Priority = Profile->GetCategoryPriority(Category);
	const float Default = FMath::Max(KINDA_SMALL_NUMBER, Profile->DefaultCategoryPriority);
	// Resistance grows with priority/default; clamp so a category never inverts the sign.
	const float Resistance = FMath::Clamp(1.f - (Default / FMath::Max(KINDA_SMALL_NUMBER, Priority)), 0.f, 1.f);
	return FMath::Clamp(GlobalPressure * (1.f - Resistance), 0.f, 1.f);
}

FString ULvl_MemoryBudgetWatcherSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("MemWatcher resident=%.0fMB budget=%.0fMB pressure=%.2f"),
		LastEstimatedResidentMB, GetEffectiveBudgetMB(), GlobalPressure);
}
