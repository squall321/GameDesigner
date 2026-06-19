// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Capability/UPlat_DeviceCapabilitySubsystem.h"
#include "Core/DPLog.h"
#include "HAL/PlatformMemory.h"
#include "GenericPlatform/GenericPlatformMemory.h"
#include "Scalability.h"

void UPlat_DeviceCapabilitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Capabilities = ResolveCapabilities();

	UE_LOG(LogDP, Log,
		TEXT("[Plat] Capabilities: Tier=%d MemMB=%d Touch=%d Rumble=%d Handheld=%d Mobile=%d Console=%d"),
		static_cast<int32>(Capabilities.PerfTier), Capabilities.PhysicalMemoryMB,
		Capabilities.bSupportsTouch, Capabilities.bSupportsGamepadRumble,
		Capabilities.bIsHandheld, Capabilities.bIsMobile, Capabilities.bIsConsole);
}

void UPlat_DeviceCapabilitySubsystem::Deinitialize()
{
	Super::Deinitialize();
}

FString UPlat_DeviceCapabilitySubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("Capability: Tier=%d Mem=%dMB Mobile=%d Console=%d Handheld=%d"),
		static_cast<int32>(Capabilities.PerfTier), Capabilities.PhysicalMemoryMB,
		Capabilities.bIsMobile, Capabilities.bIsConsole, Capabilities.bIsHandheld);
}

FPlat_DeviceCapabilities UPlat_DeviceCapabilitySubsystem::ResolveCapabilities()
{
	FPlat_DeviceCapabilities Caps;

	// Physical memory is a portable signal across every platform.
	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	const uint64 TotalPhysMB = MemStats.TotalPhysical / (1024ull * 1024ull);
	Caps.PhysicalMemoryMB = static_cast<int32>(FMath::Min<uint64>(TotalPhysMB, static_cast<uint64>(MAX_int32)));

	// Platform-class booleans. All PLATFORM_* macros are always defined, so this stays
	// compile-safe everywhere with a generic-desktop fallback.
#if PLATFORM_ANDROID || PLATFORM_IOS
	Caps.bIsMobile = true;
	Caps.bIsHandheld = true;
	Caps.bSupportsTouch = true;
	Caps.bSupportsGamepadRumble = false;
#elif PLATFORM_CONSOLE
	Caps.bIsConsole = true;
	Caps.bSupportsGamepadRumble = true;
	// Some consoles (e.g. handheld hybrids) are handheld; default to docked unless the
	// platform extension overrides this subsystem.
	Caps.bIsHandheld = false;
	Caps.bSupportsTouch = false;
#else
	// Generic desktop (Win/Mac/Linux). Treat as non-handheld with a likely gamepad+rumble path.
	Caps.bSupportsGamepadRumble = true;
	Caps.bSupportsTouch = false;
	Caps.bIsHandheld = false;
#endif

	// Derive the perf tier from platform class + memory. Mobile/console get conservative
	// defaults; desktop scales by RAM as a rough proxy for overall capability.
	if (Caps.bIsMobile)
	{
		Caps.PerfTier = (Caps.PhysicalMemoryMB >= 6144) ? EPlat_PerfTier::Medium : EPlat_PerfTier::Low;
	}
	else if (Caps.bIsConsole)
	{
		Caps.PerfTier = EPlat_PerfTier::High;
	}
	else
	{
		if (Caps.PhysicalMemoryMB >= 24576)        { Caps.PerfTier = EPlat_PerfTier::Ultra; }
		else if (Caps.PhysicalMemoryMB >= 12288)   { Caps.PerfTier = EPlat_PerfTier::High; }
		else if (Caps.PhysicalMemoryMB >= 6144)    { Caps.PerfTier = EPlat_PerfTier::Medium; }
		else                                       { Caps.PerfTier = EPlat_PerfTier::Low; }
	}

	return Caps;
}

void UPlat_DeviceCapabilitySubsystem::ApplyScalabilityHints()
{
	const int32 Quality = OnApplyScalability(Capabilities.PerfTier);
	if (Quality < 0)
	{
		UE_LOG(LogDP, Log, TEXT("[Plat] ApplyScalabilityHints skipped (designer returned -1)."));
		return;
	}

	const int32 ClampedQuality = FMath::Clamp(Quality, 0, 3);
	Scalability::FQualityLevels Levels = Scalability::GetQualityLevels();
	Levels.SetFromSingleQualityLevel(ClampedQuality);
	Scalability::SetQualityLevels(Levels);
	Scalability::SaveState(GGameUserSettingsIni);

	UE_LOG(LogDP, Log, TEXT("[Plat] Applied scalability quality level %d for tier %d."),
		ClampedQuality, static_cast<int32>(Capabilities.PerfTier));
}

int32 UPlat_DeviceCapabilitySubsystem::OnApplyScalability_Implementation(EPlat_PerfTier Tier) const
{
	switch (Tier)
	{
	case EPlat_PerfTier::Low:    return 0;
	case EPlat_PerfTier::Medium: return 1;
	case EPlat_PerfTier::High:   return 2;
	case EPlat_PerfTier::Ultra:  return 3;
	default:                     return 2;
	}
}
