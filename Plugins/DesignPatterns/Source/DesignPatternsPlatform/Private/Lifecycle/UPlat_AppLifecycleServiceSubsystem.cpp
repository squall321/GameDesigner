// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Lifecycle/UPlat_AppLifecycleServiceSubsystem.h"
#include "Lifecycle/UPlat_AppLifecycleAdapter.h"
#include "Lifecycle/UPlat_AppLifecycleSubsystem.h"
#include "Plat_NativeTags.h"

#include "Core/DPLog.h"
#include "Core/DPSubsystemLibrary.h"
#include "Services/DPServiceLocatorSubsystem.h"
#include "Engine/GameInstance.h"

void UPlat_AppLifecycleServiceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UGameInstance* GI = GetGameInstance();
	UPlat_AppLifecycleSubsystem* Lifecycle = GI ? GI->GetSubsystem<UPlat_AppLifecycleSubsystem>() : nullptr;

	// Instance the adapter as a subobject of this subsystem (proper outer for GC).
	Adapter = NewObject<UPlat_AppLifecycleAdapter>(this);
	Adapter->BindToSubsystem(Lifecycle);

	if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
	{
		// StrongOwned: the locator keeps the adapter alive for the GI's lifetime (consumers hold it weakly).
		bRegisteredService = Locator->RegisterService(
			Plat_NativeTags::Service_AppLifecycle, Adapter, EDP_ServiceLifetime::StrongOwned, /*bAllowOverride=*/true);
	}

	UE_LOG(LogDP, Log, TEXT("[Platform] AppLifecycle seam adapter registered (ok=%d)."), bRegisteredService ? 1 : 0);
}

void UPlat_AppLifecycleServiceSubsystem::Deinitialize()
{
	if (bRegisteredService)
	{
		if (UDP_ServiceLocatorSubsystem* Locator = FDP_SubsystemStatics::GetGameInstanceSubsystem<UDP_ServiceLocatorSubsystem>(this))
		{
			if (Locator->ResolveService(Plat_NativeTags::Service_AppLifecycle) == Adapter)
			{
				Locator->UnregisterService(Plat_NativeTags::Service_AppLifecycle);
			}
		}
		bRegisteredService = false;
	}

	if (Adapter)
	{
		Adapter->Shutdown();
		Adapter = nullptr;
	}

	Super::Deinitialize();
}

FString UPlat_AppLifecycleServiceSubsystem::GetDPDebugString_Implementation() const
{
	return FString::Printf(TEXT("AppLifecycleSeam registered=%d"), bRegisteredService ? 1 : 0);
}
