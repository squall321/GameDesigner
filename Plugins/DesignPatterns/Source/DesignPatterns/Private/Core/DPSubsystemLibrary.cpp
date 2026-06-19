// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Core/DPSubsystemLibrary.h"
#include "Core/DPLog.h"

UGameInstanceSubsystem* UDP_SubsystemLibrary::GetDPGameInstanceSubsystem(
	const UObject* WorldContextObject,
	TSubclassOf<UGameInstanceSubsystem> SubsystemClass)
{
	if (!SubsystemClass)
	{
		return nullptr;
	}
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}
	if (UGameInstance* GI = World->GetGameInstance())
	{
		// GetSubsystemBase returns the instance registered for the concrete class, so the
		// returned pointer genuinely matches the DeterminesOutputType pin.
		UGameInstanceSubsystem* Result = GI->GetSubsystemBase(SubsystemClass);
		ensureMsgf(Result != nullptr,
			TEXT("GetDPGameInstanceSubsystem: no subsystem of class %s (is it gated out by ShouldCreateSubsystem?)"),
			*SubsystemClass->GetName());
		return Result;
	}
	return nullptr;
}

UWorldSubsystem* UDP_SubsystemLibrary::GetDPWorldSubsystem(
	const UObject* WorldContextObject,
	TSubclassOf<UWorldSubsystem> SubsystemClass)
{
	if (!SubsystemClass)
	{
		return nullptr;
	}
	if (!GEngine || !WorldContextObject)
	{
		return nullptr;
	}
	UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
	if (!World)
	{
		return nullptr;
	}
	UWorldSubsystem* Result = World->GetSubsystemBase(SubsystemClass);
	ensureMsgf(Result != nullptr,
		TEXT("GetDPWorldSubsystem: no subsystem of class %s (is it gated out by ShouldCreateSubsystem?)"),
		*SubsystemClass->GetName());
	return Result;
}
