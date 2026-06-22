// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Capability/UPlat_DeviceCapabilitySubsystem.h"
#include "Capability/UPlat_ScalabilityTypes.h"
#include "UPlat_ScalabilitySubsystem.generated.h"

class UPlat_ScalabilityProfile;

/** Broadcast after a scalability bucket has been applied (UI can reflect the new quality). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlat_OnScalabilityApplied, EPlat_PerfTier, AppliedTier);

/**
 * Applies a data-driven scalability bucket for the current performance tier by COMPOSING with the
 * existing UPlat_DeviceCapabilitySubsystem rather than bypassing it: it reads GetPerfTier(), calls the
 * capability subsystem's ApplyScalabilityHints() FIRST (so the OnApplyScalability designer hook still
 * runs), THEN layers the authored bucket via Scalability::SetQualityLevels. Dynamic resolution is
 * driven through r.DynamicRes.* CVars via IConsoleManager (wraps the engine; no reinvention).
 *
 * Thermal / battery stepping is #ifdef-confined. Skipped on dedicated servers (no rendering).
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_ScalabilitySubsystem : public UDP_GameInstanceSubsystem
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	//~ Begin UDP_GameInstanceSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_GameInstanceSubsystem

	/** Broadcast after a bucket is applied. */
	UPROPERTY(BlueprintAssignable, Category = "Platform|Scalability")
	FPlat_OnScalabilityApplied OnScalabilityApplied;

	/** Set the active scalability profile data asset (the source of per-tier buckets). */
	UFUNCTION(BlueprintCallable, Category = "Platform|Scalability")
	void SetActiveProfile(UPlat_ScalabilityProfile* Profile);

	/** Resolve the bucket for the device's current perf tier from the active profile and apply it. */
	UFUNCTION(BlueprintCallable, Category = "Platform|Scalability")
	void ApplyProfileForCurrentTier();

	/** Apply a specific bucket immediately (composes with the capability hints first). */
	UFUNCTION(BlueprintCallable, Category = "Platform|Scalability")
	void ApplyBucket(const FPlat_ScalabilityBucket& Bucket);

	/** Toggle dynamic resolution at runtime via the engine CVars. */
	UFUNCTION(BlueprintCallable, Category = "Platform|Scalability")
	void SetDynamicResolutionEnabled(bool bEnabled);

	/**
	 * Step every scalability group up (Delta>0) or down (Delta<0), clamped to [0,4]. Re-applies the
	 * shifted levels via Scalability::SetQualityLevels. Useful for a quick "lower everything" button.
	 */
	UFUNCTION(BlueprintCallable, Category = "Platform|Scalability")
	void StepScalability(int32 Delta);

private:
	/** Push a bucket's ten group levels through Scalability::SetQualityLevels (wraps the engine). */
	void ApplyQualityLevels(const FPlat_ScalabilityBucket& Bucket);

	/** Push a bucket's dynamic-resolution bounds through the r.DynamicRes.* CVars. */
	void ApplyDynamicResolution(const FPlat_ScalabilityBucket& Bucket);

	/** Set a float CVar by name if it exists (no-op otherwise). */
	static void SetFloatCVar(const TCHAR* Name, float Value);

	/** Set an int CVar by name if it exists (no-op otherwise). */
	static void SetIntCVar(const TCHAR* Name, int32 Value);

	/** Weak: capability subsystem is engine/GI-owned. */
	TWeakObjectPtr<UPlat_DeviceCapabilitySubsystem> CapsWeak;

	/** The active profile (strong: keep it loaded while live). */
	UPROPERTY(Transient)
	TObjectPtr<UPlat_ScalabilityProfile> ActiveProfile = nullptr;
};
