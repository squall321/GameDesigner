// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "DPSubsystemLibrary.generated.h"

/**
 * Null-safe, templated accessors for DesignPatterns subsystems — the C++ side of the
 * "subsystem as singleton" convention. Callers never write raw statics; every hop is
 * guarded so a CDO/editor/early-load context returns nullptr instead of crashing.
 */
struct DESIGNPATTERNS_API FDP_SubsystemStatics
{
	/** Resolve a GameInstance subsystem from any world-context object. Null-safe at every hop. */
	template <typename T>
	static T* GetGameInstanceSubsystem(const UObject* WorldContextObject)
	{
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
			return GI->GetSubsystem<T>();
		}
		return nullptr;
	}

	/** Resolve a World subsystem from any world-context object. Null-safe at every hop. */
	template <typename T>
	static T* GetWorldSubsystem(const UObject* WorldContextObject)
	{
		if (!GEngine || !WorldContextObject)
		{
			return nullptr;
		}
		const UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull);
		return World ? World->GetSubsystem<T>() : nullptr;
	}
};

/**
 * Blueprint-facing accessors. The wildcard variants resolve the correctly-typed instance
 * via GetSubsystemBase(Class) so the returned pointer truly matches the DeterminesOutputType
 * pin — unlike returning a base pointer, which BP would unsafely reinterpret.
 */
UCLASS()
class DESIGNPATTERNS_API UDP_SubsystemLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Get a DesignPatterns (or any) GameInstance subsystem by class, correctly typed for BP. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Subsystem",
		meta = (WorldContext = "WorldContextObject", DeterminesOutputType = "SubsystemClass"))
	static UGameInstanceSubsystem* GetDPGameInstanceSubsystem(
		const UObject* WorldContextObject,
		TSubclassOf<UGameInstanceSubsystem> SubsystemClass);

	/** Get a DesignPatterns (or any) World subsystem by class, correctly typed for BP. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Subsystem",
		meta = (WorldContext = "WorldContextObject", DeterminesOutputType = "SubsystemClass"))
	static UWorldSubsystem* GetDPWorldSubsystem(
		const UObject* WorldContextObject,
		TSubclassOf<UWorldSubsystem> SubsystemClass);
};
