// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Ride/Seam_Rideable.h"
#include "WS_RideableComponent.generated.h"

class UWS_RideableComponent;
class AController;

/**
 * Static, designer-authored description of one seat on a rideable. This part is NOT replicated
 * (it is fixed setup the CDO/instance carries on both server and client); only the live OCCUPANCY
 * of a seat replicates, via FWS_SeatOccupancy below.
 *
 * The driver seat is the one whose occupant gains control of the rideable (possession handoff or
 * input forwarding). Exactly one seat should be marked bDriver for a controllable ride; a ride with
 * no driver seat (e.g. a passenger-only gondola) is valid and is simply never controllable.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLDSYSTEMS_API FWS_Seat
{
	GENERATED_BODY()

	/** Socket / bone on the rideable's mesh (or relative attach point) where an occupant is seated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|World|Ride")
	FName Socket = NAME_None;

	/** True if occupying this seat grants control of the ride (driver). Passenger seats are false. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|World|Ride")
	bool bDriver = false;

	FWS_Seat() = default;
	FWS_Seat(FName InSocket, bool bInDriver) : Socket(InSocket), bDriver(bInDriver) {}
};

/**
 * One replicated seat-occupancy row, wrapped as a fast-array item so an individual mount/dismount
 * delta-replicates instead of resending the whole table.
 *
 * Occupancy is keyed by the static seat INDEX (into the rideable's Seats array, identical on server
 * and client). The occupant is recorded two ways so clients can resolve it without a hard object
 * dependency: a net-/save-stable FSeam_EntityId (preferred, when the rider exposes one) AND a weak
 * controller reference for the driver path. Both are plain replicable value types — NO FInstancedStruct.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLDSYSTEMS_API FWS_SeatOccupancy : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Index into the owning component's Seats array. The row key; unique per occupied seat. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|World|Ride")
	int32 SeatIndex = INDEX_NONE;

	/**
	 * Stable id of the occupying rider, when the rider pawn exposes an ISeam_EntityIdentity. Invalid
	 * if the occupant has no stable id; in that case OccupantController is the only resolvable handle.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|World|Ride")
	FSeam_EntityId OccupantId;

	/**
	 * Weak reference to the occupant's controller (driver path). Replicated as an actor pointer; clients
	 * null-check it. Never dereferenced without IsValid(). Held weakly so a leaving/destroyed controller
	 * does not keep a seat falsely occupied.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|World|Ride")
	TWeakObjectPtr<AController> OccupantController;

	FWS_SeatOccupancy() = default;
	explicit FWS_SeatOccupancy(int32 InSeatIndex) : SeatIndex(InSeatIndex) {}

	//~ FFastArraySerializerItem replication callbacks (client side only).
	void PreReplicatedRemove(const struct FWS_SeatOccupancyArray& InArraySerializer);
	void PostReplicatedAdd(const struct FWS_SeatOccupancyArray& InArraySerializer);
	void PostReplicatedChange(const struct FWS_SeatOccupancyArray& InArraySerializer);
};

/**
 * Fast-array serializer holding the live occupancy rows for a rideable. NetDeltaSerialize forwards to
 * FastArrayDeltaSerialize so only changed rows cross the wire. The owning-component back-pointer is
 * non-replicated and set on both server and client so per-item callbacks can notify it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSWORLDSYSTEMS_API FWS_SeatOccupancyArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated occupancy rows (one per currently occupied seat). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|World|Ride")
	TArray<FWS_SeatOccupancy> Rows;

	/** Non-replicated back-pointer to the owning component, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<UWS_RideableComponent> OwnerComponent = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FWS_SeatOccupancy, FWS_SeatOccupancyArray>(Rows, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the seat-occupancy array. */
template<>
struct TStructOpsTypeTraits<FWS_SeatOccupancyArray> : public TStructOpsTypeTraitsBase2<FWS_SeatOccupancyArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Fired (server and client) when this rideable's occupancy changes for a specific seat. */
DECLARE_MULTICAST_DELEGATE_TwoParams(FWS_OnSeatOccupancyChanged, int32 /*SeatIndex*/, bool /*bNowOccupied*/);

/**
 * UActorComponent placed on a rideable actor (horse, car, boat) that owns the ride's seat layout and
 * authoritative occupancy, and implements ISeam_Rideable so AI mount-seekers, HUD prompts and the
 * camera can read seat/occupancy info without a hard dependency on this concrete type.
 *
 * Replication: this is a UActorComponent carrier (the net rules forbid replicated state on
 * subsystems). It replicates by default; the occupancy table delta-replicates via FFastArraySerializer.
 * Static seat layout is NOT replicated — it is identical setup on both ends.
 *
 * Mutation contract: OccupySeat / VacateSeat are AUTHORITY-ONLY and guard HasAuthority() at the top.
 * Clients never call them; entering/exiting is driven by a player-owned UWS_RiderComponent whose
 * server RPC performs validation and then calls these. This component does not itself contain any
 * client->server RPC: it only exposes the authoritative seat operations the rider's server RPC invokes.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent), HideCategories = (Variable, Sockets, Tags, ComponentReplication, Activation, Cooking, AssetUserData, Collision))
class DESIGNPATTERNSWORLDSYSTEMS_API UWS_RideableComponent : public UActorComponent, public ISeam_Rideable
{
	GENERATED_BODY()

public:
	UWS_RideableComponent();

	//~ Begin UActorComponent
	/** Wire the fast-array back-pointer and register replicated props. */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void InitializeComponent() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	//~ Begin ISeam_Rideable
	/** The kind tag of this ride (RideKind), for filtering / interaction prompts. */
	virtual FGameplayTag GetRideKind_Implementation() const override { return RideKind; }
	/** Total seats (size of the static Seats array). */
	virtual int32 GetSeatCount_Implementation() const override { return Seats.Num(); }
	/** Seats currently occupied (size of the live occupancy table). */
	virtual int32 GetOccupiedSeatCount_Implementation() const override { return Occupancy.Rows.Num(); }
	/** True if at least one seat is free. */
	virtual bool HasFreeSeat_Implementation() const override { return GetOccupiedSeatCount_Implementation() < GetSeatCount_Implementation(); }
	//~ End ISeam_Rideable

	// ---- Read-side queries (server and client) -------------------------------------------------

	/** Const access to the static seat layout. */
	const TArray<FWS_Seat>& GetSeats() const { return Seats; }

	/** Returns the seat at Index, or nullptr if out of range. */
	const FWS_Seat* GetSeat(int32 SeatIndex) const { return Seats.IsValidIndex(SeatIndex) ? &Seats[SeatIndex] : nullptr; }

	/** True if SeatIndex is a valid seat index AND it is currently free. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|World|Ride")
	bool IsSeatFree(int32 SeatIndex) const;

	/** True if SeatIndex names the driver seat. False for out-of-range or passenger seats. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|World|Ride")
	bool IsDriverSeat(int32 SeatIndex) const;

	/** Index of the driver seat, or INDEX_NONE if this ride has no driver seat (passenger-only). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|World|Ride")
	int32 GetDriverSeatIndex() const;

	/**
	 * Returns the index of the first free seat, preferring a driver seat when bPreferDriver is true
	 * (so a lone rider mounts as driver). Returns INDEX_NONE if the ride is full.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|World|Ride")
	int32 FindFreeSeat(bool bPreferDriver = true) const;

	/** Socket name for SeatIndex, or NAME_None if out of range. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|World|Ride")
	FName GetSeatSocket(int32 SeatIndex) const;

	/** Live controller occupying SeatIndex, or nullptr if free / out of range / stale. */
	AController* GetSeatOccupantController(int32 SeatIndex) const;

	/** True if Controller currently occupies any seat on this rideable. */
	bool IsControllerAboard(const AController* Controller) const;

	/** Seat index occupied by Controller, or INDEX_NONE if it is not aboard. */
	int32 FindSeatOfController(const AController* Controller) const;

	// ---- Authority-only mutation (invoked by the rider's server RPC) ---------------------------

	/**
	 * Authoritatively place an occupant in SeatIndex. Server-only: guards HasAuthority() and early-returns
	 * (returning false) on clients. Validates the seat index and that the seat is free; records the
	 * occupant's stable id (if it exposes ISeam_EntityIdentity) and its controller, marks the row dirty
	 * for delta-replication, and notifies listeners. Does NOT itself attach/possess — the rider component
	 * owns the attachment/possession handoff; this is the pure occupancy bookkeeping half.
	 * @return true if the seat was occupied; false on a client, bad index, or an already-occupied seat.
	 */
	bool OccupySeat(int32 SeatIndex, AController* Occupant);

	/**
	 * Authoritatively free SeatIndex. Server-only: guards HasAuthority() and early-returns (false) on
	 * clients. Removes the occupancy row, marks the array dirty, and notifies listeners. Idempotent: a
	 * call for an already-free seat is a no-op that returns false.
	 * @return true if a row was removed; false on a client, bad index, or an already-free seat.
	 */
	bool VacateSeat(int32 SeatIndex);

	/** Convenience: free whatever seat Controller occupies (if any). Server-only. */
	bool VacateController(AController* Controller);

	// ---- Notifications -------------------------------------------------------------------------

	/** Multicast (server + client) fired when a seat becomes occupied or freed. */
	FWS_OnSeatOccupancyChanged OnSeatOccupancyChanged;

	/** Called by the fast-array item callbacks on clients to refire OnSeatOccupancyChanged. */
	void HandleReplicatedOccupancyChange(int32 SeatIndex, bool bNowOccupied);

protected:
	/**
	 * Kind tag for this ride (child of WS.Ride.Kind, e.g. WS.Ride.Kind.Horse). Read through the seam for
	 * filtering and prompts. Not replicated — static designer setup identical on both ends.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|World|Ride", meta = (Categories = "WS.Ride.Kind"))
	FGameplayTag RideKind;

	/**
	 * The static seat layout. Order is significant: seat index is the replication key, so it MUST be
	 * identical on server and client (it is — this is designer setup, not runtime state). Author at least
	 * one entry with bDriver=true for a controllable ride.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|World|Ride")
	TArray<FWS_Seat> Seats;

	/** Live, delta-replicated occupancy table. Authoritative on the server; read-only mirror on clients. */
	UPROPERTY(Replicated, Transient)
	FWS_SeatOccupancyArray Occupancy;

private:
	/** Finds the occupancy row for SeatIndex, or nullptr if that seat is free. */
	const FWS_SeatOccupancy* FindRow(int32 SeatIndex) const;
	FWS_SeatOccupancy* FindRowMutable(int32 SeatIndex);
};
