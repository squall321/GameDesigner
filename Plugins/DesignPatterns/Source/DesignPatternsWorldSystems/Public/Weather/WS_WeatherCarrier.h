// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "WS_WeatherCarrier.generated.h"

class UWS_WeatherSubsystem;

/**
 * Fired (server and clients) after the replicated weather state tag changes on this carrier. Lets the
 * owning weather subsystem react uniformly on both authority and remotes via the same code path.
 * @param Carrier  The carrier whose state changed.
 * @param NewState The weather state now active.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWS_OnCarrierStateChanged,
	AWS_WeatherCarrier*, Carrier, FGameplayTag, NewState);

/**
 * Replicated authority carrier for the world's CURRENT weather state.
 *
 * Subsystems are never replicated, so the weather subsystem keeps no replicated state of its own;
 * instead it spawns exactly one of these tiny AInfo carriers on the server and routes its authoritative
 * SetWeather through the carrier's HasAuthority-guarded mutator. The single lightweight state tag
 * replicates to clients, whose OnRep fans the change back to the subsystem (and to listeners) so the
 * cosmetic response runs identically everywhere.
 *
 * Only a tag (and a monotonically-increasing change counter, to force an OnRep even on a same-tag
 * re-apply) replicates — never any FInstancedStruct, never any heavy cosmetic data. The carrier sits
 * net-dormant and only wakes when the state actually changes.
 */
UCLASS(NotPlaceable)
class DESIGNPATTERNSWORLDSYSTEMS_API AWS_WeatherCarrier : public AInfo
{
	GENERATED_BODY()

public:
	AWS_WeatherCarrier();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	//~ End AActor

	/**
	 * Authoritative setter for the current weather state. AUTHORITY ONLY — early-returns on clients.
	 * Stores the tag, bumps the change counter (so even a same-tag re-apply triggers OnRep), wakes the
	 * actor from dormancy, and locally fires OnStateChanged so the server reacts on the same path as
	 * clients.
	 * @return True if applied (false on clients).
	 */
	bool AuthSetState(const FGameplayTag& NewState);

	/** The replicated current weather state tag (invalid = no active weather). Client-safe read. */
	UFUNCTION(BlueprintPure, Category = "WorldSystems|Weather")
	FGameplayTag GetCurrentState() const { return CurrentStateTag; }

	/** Fired (server and clients) whenever the replicated weather state changes. */
	UPROPERTY(BlueprintAssignable, Category = "WorldSystems|Weather")
	FWS_OnCarrierStateChanged OnStateChanged;

private:
	/** Replicated current weather state tag. Mutated only by AuthSetState (authority). */
	UPROPERTY(ReplicatedUsing = OnRep_State)
	FGameplayTag CurrentStateTag;

	/**
	 * Monotonic change counter, replicated alongside the tag. Guarantees clients receive an OnRep even
	 * when the state is re-applied to the same tag (e.g. a forced refresh), since the tag alone would
	 * not have changed.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_State)
	int32 StateRevision = 0;

	/** OnRep for the weather state (both CurrentStateTag and StateRevision use it). Fires OnStateChanged. */
	UFUNCTION()
	void OnRep_State();
};
