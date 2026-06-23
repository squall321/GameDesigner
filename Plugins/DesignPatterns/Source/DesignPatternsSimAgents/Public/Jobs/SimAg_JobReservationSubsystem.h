// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Persist/Seam_Persistable.h"
#include "Jobs/Seam_JobReservation.h"
#include "Jobs/SimAg_ReservationArray.h"
#include "SimAg_JobReservationSubsystem.generated.h"

class ASimAg_ReservationReplicator;
class USimAg_ClockSubsystem;

/**
 * One persisted reservation. Mirrors the replicated FSimAg_Reservation in a plain SaveGame struct.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_SavedReservation
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FSeam_EntityId Target;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	FSeam_EntityId Agent;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	double ExpiryDays = 0.0;
};

/**
 * The reservation subsystem's CaptureState record: every active reservation at capture time. The concrete
 * type written into the ISeam_Persistable FInstancedStruct out-parameter.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMAGENTS_API FSimAg_ReservationRecord
{
	GENERATED_BODY()

	/** Every active reservation at capture time. */
	UPROPERTY(SaveGame, BlueprintReadOnly, Category = "SimAgents|Save")
	TArray<FSimAg_SavedReservation> Reservations;
};

/**
 * World-scoped reservation router. Lazily spawns ONE ASimAg_ReservationReplicator on authority and routes
 * reserve/release/query so two agents never claim the same stockpile / workstation, complementing the job
 * board's posting machinery. Implements the shared ISeam_JobReservation seam (so haul / job-chain
 * behaviours lock resources without depending on this concrete type) and ISeam_Persistable.
 *
 * Holds NO replicated state itself (subsystems are never replicated). Registers under
 * SimAgNativeTags::Service_JobReservation (WeakObserved). Reservations carry an expiry on the simulation
 * clock so a dead/stuck agent never permanently locks a target. Expired reservations are pruned lazily on
 * each query/reserve, so there is no ticker to register/unregister.
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API USimAg_JobReservationSubsystem
	: public UDP_WorldSubsystem
	, public ISeam_JobReservation
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority; declare our own (mirrors USimAg_JobBoardSubsystem). True
	 * on the server / standalone, gating all carrier spawning and mutation.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	//~ Begin ISeam_JobReservation
	virtual bool TryReserve_Implementation(FSeam_EntityId Target, FSeam_EntityId Agent) override;
	virtual void Release_Implementation(FSeam_EntityId Target) override;
	virtual bool IsReserved_Implementation(FSeam_EntityId Target) const override;
	//~ End ISeam_JobReservation

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	/** Who currently holds Target, or an invalid id if free/expired. Client-safe read. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Jobs")
	FSeam_EntityId GetReservationHolder(FSeam_EntityId Target) const;

	/** Number of live reservations right now. Client-safe. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimAgents|Jobs")
	int32 GetReservationCount() const;

	/** The live carrier, spawning it on authority if it does not yet exist. Null on clients with none. */
	ASimAg_ReservationReplicator* GetOrSpawnCarrier(bool bSpawnIfMissing);

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/**
	 * The single reservation carrier. Owned by the world (runtime, transient actor); held WEAK
	 * (non-owning), always null-checked. NON-replicated — the subsystem never crosses the wire.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ASimAg_ReservationReplicator> Carrier;

	/** Service-locator key we registered under, for clean unregister on teardown. */
	UPROPERTY(Transient)
	FGameplayTag RegisteredServiceTag;

	/** Weak, non-owning handle to the world clock; resolved lazily. */
	UPROPERTY(Transient)
	TWeakObjectPtr<USimAg_ClockSubsystem> CachedClock;

	/** Cached reservation expiry (sim days) from settings, refreshed at Initialize. */
	float ExpiryDays = 0.25f;

	/** Discover an already-replicated carrier on clients, or cache the authority one. */
	ASimAg_ReservationReplicator* ResolveCarrier() const;

	/** Register this subsystem under the reservation service tag (WeakObserved). */
	void RegisterAsReservationProvider();

	/** Resolve (and cache) the world clock subsystem. Null-safe. */
	USimAg_ClockSubsystem* GetClock() const;

	/** Current simulation time in fractional DAYS (clock day + time-of-day). 0 if no clock. */
	double GetNowDays() const;
};
