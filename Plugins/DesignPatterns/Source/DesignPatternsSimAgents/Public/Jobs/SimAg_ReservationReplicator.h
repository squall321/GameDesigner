// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Identity/Seam_EntityId.h"
#include "Jobs/SimAg_ReservationArray.h"
#include "SimAg_ReservationReplicator.generated.h"

/**
 * Fired (server and clients) whenever the reservation set changes on this carrier — after replication on
 * clients. Carries the affected target id.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimAg_OnReservationChanged,
	ASimAg_ReservationReplicator*, Carrier, FSeam_EntityId, Target);

/**
 * Replicated authority carrier for the world's reservation set.
 *
 * SimAgents subsystems are never replicated; all authoritative reservation state lives on this AInfo.
 * The reservation world subsystem spawns exactly one carrier on the server and routes every mutation
 * through this actor's authority-guarded mutators. Clients receive deltas via the FFastArraySerializer.
 *
 * Net dormancy: the carrier sits DORMANT_Initial and only flushes dormancy when reservations actually
 * change (mirrors ASimAg_JobBoardReplicator).
 */
UCLASS()
class DESIGNPATTERNSSIMAGENTS_API ASimAg_ReservationReplicator : public AInfo
{
	GENERATED_BODY()

public:
	ASimAg_ReservationReplicator();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	//~ End AActor

	// --- Authority mutators (AUTHORITY ONLY; each early-returns on clients) ---

	/**
	 * Reserve Target for Agent until NowDays + the configured expiry (passed as absolute ExpiryDays).
	 * AUTHORITY ONLY. Succeeds if Target is unreserved, the existing reservation has expired (relative to
	 * NowDays), or Target is already reserved by Agent (idempotent refresh). @return true on success.
	 */
	bool Reserve(const FSeam_EntityId& Target, const FSeam_EntityId& Agent, double ExpiryDays, double NowDays);

	/** Release any reservation on Target. AUTHORITY ONLY. @return true if one existed and was removed. */
	bool Release(const FSeam_EntityId& Target);

	/** Drop every reservation whose expiry has passed relative to NowDays. AUTHORITY ONLY. @return count. */
	int32 PruneExpired(double NowDays);

	/** Clear all reservations (e.g. on save restore before re-seeding). AUTHORITY ONLY. */
	void ClearAll();

	/** Restore a single reservation verbatim (save path). AUTHORITY ONLY. */
	void RestoreReservation(const FSimAg_Reservation& Reservation);

	// --- Reads (client-safe) ---

	/** Find the reservation for Target, or null. Const, client-safe. */
	const FSimAg_Reservation* Find(const FSeam_EntityId& Target) const;

	/** Read-only access to all reservations on this carrier. */
	const TArray<FSimAg_Reservation>& GetReservations() const { return Reservations.Reservations; }

	/** Fired when any reservation changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimAgents|Jobs")
	FSimAg_OnReservationChanged OnReservationChanged;

	/** Called by the fast-array callbacks on clients to surface a replicated change. */
	void HandleReplicatedChange(const FSeam_EntityId& Target);

private:
	/** Replicated reservations (delta-serialized). */
	UPROPERTY(Replicated)
	FSimAg_ReservationArray Reservations;

	/** Mutable find for the authority mutators; null if absent. */
	FSimAg_Reservation* FindMutable(const FSeam_EntityId& Target);

	/** Wake the actor from net dormancy so a just-changed delta replicates this frame. */
	void WakeForChange();
};
