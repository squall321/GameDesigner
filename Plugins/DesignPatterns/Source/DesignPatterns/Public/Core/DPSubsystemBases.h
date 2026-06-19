// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Subsystems/WorldSubsystem.h"
#include "DPSubsystemBases.generated.h"

/**
 * Thin abstract base for all DesignPatterns GameInstance-scoped singletons.
 *
 * Standardizes lifecycle logging, exposes a Blueprint-overridable debug string consumed
 * by the on-screen debugger / gameplay debugger, and provides a verbose-logging toggle.
 * Using the engine's subsystem machinery (instead of raw static singletons) means the
 * engine owns lifetime and GC, and games can subclass/replace via project settings.
 */
UCLASS(Abstract)
class DESIGNPATTERNS_API UDP_GameInstanceSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** Override to produce a one-line status string for on-screen / gameplay-debugger output. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Debug")
	FString GetDPDebugString() const;
	virtual FString GetDPDebugString_Implementation() const;

protected:
	/** When true this subsystem logs at Verbose. Toggle per-subsystem in defaults or at runtime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DesignPatterns")
	bool bEnableVerboseLogging = false;
};

/**
 * Thin abstract base for all DesignPatterns World-scoped singletons.
 *
 * World subsystems are created per-world and torn down with the world, which is exactly
 * the right lifetime for pools, FSMs and anything that must not leak across level travel.
 * Override ShouldCreateSubsystem to gate creation by world type (e.g. exclude editor preview).
 */
UCLASS(Abstract)
class DESIGNPATTERNS_API UDP_WorldSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	//~ End USubsystem

	/** Override to produce a one-line status string for on-screen / gameplay-debugger output. */
	UFUNCTION(BlueprintNativeEvent, Category = "DesignPatterns|Debug")
	FString GetDPDebugString() const;
	virtual FString GetDPDebugString_Implementation() const;

protected:
	/**
	 * By default DP world subsystems are created only for Game and PIE worlds (not editor
	 * preview / inactive worlds). Subclasses can widen this by overriding ShouldCreateSubsystem.
	 */
	virtual bool IsSupportedWorldType(const EWorldType::Type WorldType) const;

	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "DesignPatterns")
	bool bEnableVerboseLogging = false;
};
