// Copyright DesignPatterns plugin. All Rights Reserved.

#include "Query/WorldHub_QueryLibrary.h"
#include "Hub/WorldHub_StateHubSubsystem.h"
#include "Hub/WorldHub_GameStateHubSubsystem.h"
#include "Core/DPSubsystemLibrary.h"

UWorldHub_StateHubSubsystem* UWorldHub_QueryLibrary::GetWorldHub(const UObject* WorldContextObject)
{
	return FDP_SubsystemStatics::GetWorldSubsystem<UWorldHub_StateHubSubsystem>(WorldContextObject);
}

UWorldHub_GameStateHubSubsystem* UWorldHub_QueryLibrary::GetGameStateHub(const UObject* WorldContextObject)
{
	return FDP_SubsystemStatics::GetGameInstanceSubsystem<UWorldHub_GameStateHubSubsystem>(WorldContextObject);
}

bool UWorldHub_QueryLibrary::GetFlag(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope, bool bDefault)
{
	const UWorldHub_StateHubSubsystem* Hub = GetWorldHub(WorldContextObject);
	return Hub ? Hub->QueryFlag(Key, Scope, bDefault) : bDefault;
}

int64 UWorldHub_QueryLibrary::GetCounter(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope, int64 Default)
{
	const UWorldHub_StateHubSubsystem* Hub = GetWorldHub(WorldContextObject);
	return Hub ? Hub->QueryCounter(Key, Scope, Default) : Default;
}

bool UWorldHub_QueryLibrary::GetValue(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope, FInstancedStruct& Out)
{
	const UWorldHub_StateHubSubsystem* Hub = GetWorldHub(WorldContextObject);
	return Hub ? Hub->QueryValue(Key, Scope, Out) : false;
}

bool UWorldHub_QueryLibrary::HasValue(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope)
{
	const UWorldHub_StateHubSubsystem* Hub = GetWorldHub(WorldContextObject);
	return Hub ? Hub->HasValue(Key, Scope) : false;
}

void UWorldHub_QueryLibrary::SetFlag(const UObject* WorldContextObject, FGameplayTag Key, bool bValue, FWorldHub_Scope Scope)
{
	if (UWorldHub_StateHubSubsystem* Hub = GetWorldHub(WorldContextObject))
	{
		// Authority is guarded inside the subsystem; this is a safe no-op on clients.
		Hub->SetFlag(Key, bValue, Scope);
	}
}

int64 UWorldHub_QueryLibrary::IncrementCounter(const UObject* WorldContextObject, FGameplayTag Key, int64 Delta, FWorldHub_Scope Scope)
{
	if (UWorldHub_StateHubSubsystem* Hub = GetWorldHub(WorldContextObject))
	{
		return Hub->IncrementCounter(Key, Delta, Scope);
	}
	return 0;
}

void UWorldHub_QueryLibrary::SetNetValue(const UObject* WorldContextObject, FGameplayTag Key, FSeam_NetValue Value, FWorldHub_Scope Scope)
{
	if (UWorldHub_StateHubSubsystem* Hub = GetWorldHub(WorldContextObject))
	{
		Hub->SetNetValue(Key, Value, Scope);
	}
}

void UWorldHub_QueryLibrary::ClearValue(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope)
{
	if (UWorldHub_StateHubSubsystem* Hub = GetWorldHub(WorldContextObject))
	{
		Hub->ClearValue(Key, Scope);
	}
}
