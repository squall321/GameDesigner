// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "UPlat_AppLifecycleServiceSubsystem.generated.h"

class UPlat_AppLifecycleAdapter;

/**
 * Thin GameInstance subsystem that publishes the app-lifecycle seam. It instances a
 * UPlat_AppLifecycleAdapter (NewObject with this as outer), binds it to the shipped
 * UPlat_AppLifecycleSubsystem, and registers it StrongOwned under DP.Service.Platform.AppLifecycle so
 * GameFlow's pause controller can resolve ISeam_AppLifecycle by tag. Unregisters + shuts the adapter
 * down in Deinitialize so nothing leaks across travel.
 *
 * This exists as a separate subsystem (rather than the existing lifecycle subsystem registering itself)
 * to keep the shipped UPlat_AppLifecycleSubsystem signatures untouched (additive rule) and to avoid a
 * subsystem holding a strong service reference to itself.
 */
UCLASS()
class DESIGNPATTERNSPLATFORM_API UPlat_AppLifecycleServiceSubsystem : public UDP_GameInstanceSubsystem
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

private:
	/** The seam adapter instance (kept as a transient subobject; the locator holds the StrongOwned ref). */
	UPROPERTY(Transient)
	TObjectPtr<UPlat_AppLifecycleAdapter> Adapter = nullptr;

	/** True once the adapter was registered. */
	bool bRegisteredService = false;
};
