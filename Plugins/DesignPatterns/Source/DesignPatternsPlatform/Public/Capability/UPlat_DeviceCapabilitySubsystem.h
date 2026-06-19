// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UPlat_DeviceCapabilitySubsystem.generated.h"

/**
 * Coarse performance / feature tier the game should target on this device.
 *
 * Derived once from platform class + physical memory (+ optional RHI/feature-level signal).
 * Games map this to scalability buckets, LOD bias, effect density, etc.
 */
UENUM(BlueprintType)
enum class EPlat_PerfTier : uint8
{
	/** Low-end mobile / minimum-spec: aggressive cuts. */
	Low		UMETA(DisplayName = "Low"),
	/** Mid mobile / older console / modest PC. */
	Medium	UMETA(DisplayName = "Medium"),
	/** Current-gen console / typical gaming PC. */
	High	UMETA(DisplayName = "High"),
	/** High-end PC / dev kits: everything on. */
	Ultra	UMETA(DisplayName = "Ultra")
};

/**
 * A snapshot of the resolved device capabilities. Plain UStruct so it can be passed to
 * Blueprint, logged, or fed into a save profile.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSPLATFORM_API FPlat_DeviceCapabilities
{
	GENERATED_BODY()

	/** Resolved performance/feature tier. */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Capability")
	EPlat_PerfTier PerfTier = EPlat_PerfTier::High;

	/** Physical memory in megabytes, as reported by the platform (0 if unknown). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Capability")
	int32 PhysicalMemoryMB = 0;

	/** Device exposes a touch screen. */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Capability")
	bool bSupportsTouch = false;

	/** A connected gamepad can deliver force feedback / rumble. */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Capability")
	bool bSupportsGamepadRumble = false;

	/** Handheld form factor (Switch, Steam Deck, phone, tablet). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Capability")
	bool bIsHandheld = false;

	/** Mobile OS (Android / iOS). */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Capability")
	bool bIsMobile = false;

	/** Console platform. */
	UPROPERTY(BlueprintReadOnly, Category = "Platform|Capability")
	bool bIsConsole = false;
};

/**
 * Resolves and caches the device's performance/feature tier and capability booleans.
 *
 * Detection is confined to this module (platform #ifdefs + FPlatformMemory). Games read the
 * tier to drive scalability hints; ApplyScalabilityHints() pushes a sane default scalability
 * level for the tier (overridable by the OnApplyScalability designer hook).
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_DeviceCapabilitySubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** The full resolved capability snapshot. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Capability")
	const FPlat_DeviceCapabilities& GetCapabilities() const { return Capabilities; }

	/** Resolved performance/feature tier. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Capability")
	EPlat_PerfTier GetPerfTier() const { return Capabilities.PerfTier; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Capability")
	bool SupportsTouch() const { return Capabilities.bSupportsTouch; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Capability")
	bool SupportsGamepadRumble() const { return Capabilities.bSupportsGamepadRumble; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Capability")
	bool IsHandheld() const { return Capabilities.bIsHandheld; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Capability")
	bool IsMobile() const { return Capabilities.bIsMobile; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Capability")
	bool IsConsole() const { return Capabilities.bIsConsole; }

	/**
	 * Push a scalability level (0..3 ~ Low..Epic) derived from the perf tier to the engine.
	 * Calls the OnApplyScalability designer hook first so projects can override the mapping.
	 */
	UFUNCTION(BlueprintCallable, Category = "Platform|Capability")
	void ApplyScalabilityHints();

	/**
	 * Designer override: map a perf tier to an engine scalability quality level (0..3).
	 * Return -1 to skip applying scalability entirely. Default maps Low..Ultra -> 0..3.
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Platform|Capability")
	int32 OnApplyScalability(EPlat_PerfTier Tier) const;
	virtual int32 OnApplyScalability_Implementation(EPlat_PerfTier Tier) const;

private:
	/** Compute the capability snapshot from the current platform. */
	static FPlat_DeviceCapabilities ResolveCapabilities();

	/** Cached snapshot, resolved once in Initialize. */
	UPROPERTY()
	FPlat_DeviceCapabilities Capabilities;
};
