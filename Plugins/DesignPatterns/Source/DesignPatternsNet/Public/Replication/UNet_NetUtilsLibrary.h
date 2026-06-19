// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineTypes.h"
#include "UNet_NetUtilsLibrary.generated.h"

class AActor;
class APawn;

/**
 * Mirror of ENetMode exposed to Blueprint, so net-mode branching can be authored without
 * raw enum casts. Values intentionally match the engine's ENetMode ordering.
 */
UENUM(BlueprintType)
enum class ENet_NetMode : uint8
{
	/** No network: a single, self-contained game.   */
	Standalone     UMETA(DisplayName = "Standalone"),
	/** Dedicated server: no local player, authority.  */
	DedicatedServer UMETA(DisplayName = "Dedicated Server"),
	/** Listen server: authority + a local player.     */
	ListenServer   UMETA(DisplayName = "Listen Server"),
	/** Remote client: no authority.                   */
	Client         UMETA(DisplayName = "Client"),
	/** Net mode could not be resolved (no world).      */
	Unknown        UMETA(DisplayName = "Unknown")
};

/**
 * Stateless authority / network-role helpers that codify the checks the core's components use
 * inline. Gameplay code should prefer these over hand-rolled GetOwner()->HasAuthority() chains
 * so the null-safety and intent are consistent everywhere.
 *
 * NOTE: "authority" here means *network authority* (the server, or a standalone game). It is the
 * gate the core's HARD RULE 6 requires at the top of every replicated-state mutator.
 */
UCLASS()
class DESIGNPATTERNSNET_API UNet_NetUtilsLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * True if the actor holds network authority (server side, or standalone). Null-safe: a null
	 * actor returns false (fail-closed — never mutate replicated state without a confirmed owner).
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Authority")
	static bool HasAuthority(const AActor* Actor);

	/**
	 * True if the pawn is locally controlled on this machine. Null-safe. Use to gate input,
	 * first-person cosmetic effects, and client-prediction entry points.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Authority")
	static bool IsLocallyControlled(const APawn* Pawn);

	/** True if the actor's role is ROLE_AutonomousProxy (the client that owns/predicts it). Null-safe. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Role")
	static bool IsAutonomousProxy(const AActor* Actor);

	/** True if the actor's role is ROLE_SimulatedProxy (a remote actor this machine only observes). Null-safe. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Role")
	static bool IsSimulatedProxy(const AActor* Actor);

	/** True when there is no networking at all (standalone game). Null-safe (false without a world). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Role")
	static bool IsStandalone(const AActor* Actor);

	/** True on a dedicated server (no local viewport/player). Null-safe. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Role")
	static bool IsDedicatedServer(const AActor* Actor);

	/** Resolve the actor's net mode as the Blueprint-friendly ENet_NetMode. Unknown if no world. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Role")
	static ENet_NetMode GetNetMode(const AActor* Actor);

	/**
	 * One-line human-readable network description of the actor (mode + role + authority), for
	 * on-screen debug / logging. Never crashes on a null actor.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Net|Debug")
	static FString DescribeNetContext(const AActor* Actor);

public:
	/**
	 * Templated server-authority assert helper for C++ gameplay code. Returns true (and lets the
	 * caller proceed) ONLY when the owning actor has authority; otherwise logs a warning naming the
	 * call site and returns false so the caller can early-out.
	 *
	 * Canonical usage at the TOP of any replicated-state mutator (HARD RULE 6):
	 *
	 *     void UMyComp::ServerSideMutate()
	 *     {
	 *         if (!UNet_NetUtilsLibrary::EnsureAuthority(GetOwner(), TEXT("UMyComp::ServerSideMutate")))
	 *         {
	 *             return;
	 *         }
	 *         // ... safe to mutate replicated state here ...
	 *     }
	 *
	 * This is the function-template companion to the inline guard the core uses; both express the
	 * same fail-closed contract so a client NEVER mutates replicated state.
	 */
	static bool EnsureAuthority(const AActor* OwnerActor, const TCHAR* CallSite = TEXT("<unknown>"));
};
