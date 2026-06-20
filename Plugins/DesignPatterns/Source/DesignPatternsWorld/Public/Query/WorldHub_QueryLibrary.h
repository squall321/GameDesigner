// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayTagContainer.h"
#include "Hub/WorldHub_Scope.h"
#include "Net/Seam_NetValue.h"

// FInstancedStruct lives in StructUtils on UE 5.3/5.4, merged into CoreUObject in 5.5.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "WorldHub_QueryLibrary.generated.h"

class UWorldHub_StateHubSubsystem;
class UWorldHub_GameStateHubSubsystem;

/**
 * World-context convenience helpers for reading and (authoritatively) writing world-hub state from
 * Blueprint and C++ without first resolving the subsystem by hand.
 *
 * Every accessor is null-safe at each hop (CDO/editor/early-load returns the default), and every
 * mutator routes through the subsystem's authority-guarded API — so a client call is a safe no-op,
 * never a crash. These are thin wrappers; gameplay that needs the subsystem repeatedly should cache
 * the resolved pointer from GetWorldHub.
 */
UCLASS()
class DESIGNPATTERNSWORLD_API UWorldHub_QueryLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Resolve the world state-hub subsystem from any world-context object (null-safe). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub", meta = (WorldContext = "WorldContextObject"))
	static UWorldHub_StateHubSubsystem* GetWorldHub(const UObject* WorldContextObject);

	/** Resolve the persistent game-instance hub subsystem from any world-context object (null-safe). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub", meta = (WorldContext = "WorldContextObject"))
	static UWorldHub_GameStateHubSubsystem* GetGameStateHub(const UObject* WorldContextObject);

	// ---- Reads (safe on clients) --------------------------------------------------------------

	/** Read a boolean flag at the given scope, with Entity/Faction -> Global fallback. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|WorldHub",
		meta = (WorldContext = "WorldContextObject"))
	static bool GetFlag(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope, bool bDefault = false);

	/** Read a counter at the given scope, with fallback. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|WorldHub",
		meta = (WorldContext = "WorldContextObject"))
	static int64 GetCounter(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope, int64 Default = 0);

	/** Read a raw value at the given scope into Out. @return true if a stored value exists. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub",
		meta = (WorldContext = "WorldContextObject"))
	static bool GetValue(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope, FInstancedStruct& Out);

	/** True if a concrete value is stored for (Key, Scope) after fallback. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|WorldHub",
		meta = (WorldContext = "WorldContextObject"))
	static bool HasValue(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope);

	// ---- Authoritative writes (no-op on clients) ----------------------------------------------

	/** Set a boolean flag (AUTHORITY ONLY). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub",
		meta = (WorldContext = "WorldContextObject"))
	static void SetFlag(const UObject* WorldContextObject, FGameplayTag Key, bool bValue, FWorldHub_Scope Scope);

	/** Add Delta to a counter and return the new value (AUTHORITY ONLY; returns current/0 on clients). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub",
		meta = (WorldContext = "WorldContextObject"))
	static int64 IncrementCounter(const UObject* WorldContextObject, FGameplayTag Key, int64 Delta, FWorldHub_Scope Scope);

	/** Set a net-friendly value (AUTHORITY ONLY). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub",
		meta = (WorldContext = "WorldContextObject"))
	static void SetNetValue(const UObject* WorldContextObject, FGameplayTag Key, FSeam_NetValue Value, FWorldHub_Scope Scope);

	/** Clear a value (AUTHORITY ONLY). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|WorldHub",
		meta = (WorldContext = "WorldContextObject"))
	static void ClearValue(const UObject* WorldContextObject, FGameplayTag Key, FWorldHub_Scope Scope);

	// ---- Scope construction helpers (BP-friendly) ---------------------------------------------

	/** Construct the single global scope. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|Scope")
	static FWorldHub_Scope MakeGlobalScope() { return FWorldHub_Scope::Global(); }

	/** Construct a faction scope from a faction tag. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|Scope")
	static FWorldHub_Scope MakeFactionScope(FGameplayTag FactionTag) { return FWorldHub_Scope::Faction(FactionTag); }

	/** Construct an entity scope from a net-/save-stable entity id. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|WorldHub|Scope")
	static FWorldHub_Scope MakeEntityScope(FSeam_EntityId EntityId) { return FWorldHub_Scope::Entity(EntityId); }
};
