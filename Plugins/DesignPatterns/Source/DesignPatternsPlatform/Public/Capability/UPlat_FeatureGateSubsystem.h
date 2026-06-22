// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "UPlat_FeatureGateSubsystem.generated.h"

/** Broadcast when online/store/presence availability changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPlat_OnOnlineStateChanged, bool, bOnline);

/**
 * Platform feature gating: online / store / presence availability plus a tag-keyed feature query.
 * All platform probing is #ifdef-confined with a generic editor "available" fallback; designer
 * overrides come from UPlat_FeatureGateSettings, and a project can veto any feature via the
 * OnQueryFeature BlueprintNativeEvent hook (default returns the platform default).
 *
 * Online connectivity is a project-pushed signal (like UPlat_InputRouterSubsystem's device push):
 * the host's online layer calls NotifyOnlineState when connectivity flips, and the gate coalesces +
 * re-broadcasts it. This avoids depending on version-variable engine connectivity delegates while
 * keeping all platform branching confined to this module.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_FeatureGateSubsystem : public UDP_GameInstanceSubsystem
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

	/** Broadcast when online availability flips. */
	UPROPERTY(BlueprintAssignable, Category = "Platform|Features")
	FPlat_OnOnlineStateChanged OnOnlineStateChanged;

	/** True when the device currently has online connectivity. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Features")
	bool IsOnline() const;

	/** True when an in-game store / commerce service is available. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Features")
	bool IsStoreAvailable() const;

	/** True when rich-presence reporting is available. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Features")
	bool IsPresenceAvailable() const;

	/**
	 * Query whether a tag-keyed feature is available: a hard override in settings wins; otherwise the
	 * platform default is computed and then passed through the OnQueryFeature designer hook.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Features")
	bool IsFeatureAvailable(FGameplayTag FeatureTag) const;

	/**
	 * Designer veto/override for a feature. Default returns bPlatformDefault unchanged. Projects can
	 * disable a feature regardless of platform capability (e.g. gate cross-play behind a setting).
	 */
	UFUNCTION(BlueprintNativeEvent, Category = "Platform|Features")
	bool OnQueryFeature(FGameplayTag FeatureTag, bool bPlatformDefault) const;
	virtual bool OnQueryFeature_Implementation(FGameplayTag FeatureTag, bool bPlatformDefault) const;

	/**
	 * Push the live online connectivity state from the host's online layer (e.g. an OSS connectivity
	 * callback). Only broadcasts OnOnlineStateChanged when the state actually flips. This is the
	 * recommended live path; the initial state is the platform-computed default.
	 */
	UFUNCTION(BlueprintCallable, Category = "Platform|Features")
	void NotifyOnlineState(bool bOnline);

private:
	/** Compute the raw platform availability of online at startup (no overrides/hook). */
	bool ComputePlatformOnline() const;

	/** Last-known online state (for change detection). */
	bool bLastOnline = false;
};
