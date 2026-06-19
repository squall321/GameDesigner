// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Delegates/IDelegateInstance.h"
#include "UPlat_AppLifecycleSubsystem.generated.h"

/** Fired when the app is suspended / backgrounded / deactivated (good time to auto-pause+save). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlat_OnAppSuspended);

/** Fired when the app is resumed / returns to the foreground (good time to auto-unpause). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPlat_OnAppResumed);

/**
 * Re-broadcasts engine application lifecycle (mobile suspend/resume, console constrained)
 * as Blueprint-friendly dynamic delegates so games can auto-pause / auto-save without
 * touching FCoreDelegates directly.
 *
 * Subscribes in Initialize to:
 *   - ApplicationWillEnterBackgroundDelegate  -> OnAppSuspended
 *   - ApplicationWillDeactivateDelegate       -> OnAppSuspended (e.g. console constrained, alt-tab)
 *   - ApplicationHasEnteredForegroundDelegate -> OnAppResumed
 *   - ApplicationHasReactivatedDelegate       -> OnAppResumed
 * ALL handles are unsubscribed in Deinitialize. IsSuspended() is debounced so repeated
 * background/deactivate events do not double-broadcast.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_AppLifecycleSubsystem : public UDP_GameInstanceSubsystem
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

	/** Broadcast when the app is suspended / backgrounded / deactivated. */
	UPROPERTY(BlueprintAssignable, Category = "Platform|Lifecycle")
	FPlat_OnAppSuspended OnAppSuspended;

	/** Broadcast when the app is resumed / foregrounded / reactivated. */
	UPROPERTY(BlueprintAssignable, Category = "Platform|Lifecycle")
	FPlat_OnAppResumed OnAppResumed;

	/** True between a suspend and the matching resume. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Platform|Lifecycle")
	bool IsSuspended() const { return bIsSuspended; }

private:
	/** Internal FCoreDelegates handlers (debounced via bIsSuspended). */
	void HandleWillEnterBackground();
	void HandleWillDeactivate();
	void HandleEnteredForeground();
	void HandleReactivated();

	/** Common debounced transitions. */
	void EnterSuspended();
	void ExitSuspended();

	/** Live debounce flag. */
	bool bIsSuspended = false;

	//~ FCoreDelegates handles, all cleared in Deinitialize.
	FDelegateHandle WillEnterBackgroundHandle;
	FDelegateHandle WillDeactivateHandle;
	FDelegateHandle EnteredForegroundHandle;
	FDelegateHandle ReactivatedHandle;
};
