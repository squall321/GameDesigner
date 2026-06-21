// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "WS_RiderComponent.generated.h"

class UWS_RideableComponent;
class APawn;
class AController;

/**
 * How the driver of a ride is given control of it. Authored per-rider (or per-ride via the rideable),
 * chosen here on the rider because it determines what the rider's pawn does on enter/exit.
 */
UENUM(BlueprintType)
enum class EWS_DriverControlMode : uint8
{
	/**
	 * Possession hand-off: the driver's controller un-possesses the rider pawn and possesses the
	 * rideable actor (which must itself be a Pawn). On exit the controller re-possesses the original
	 * rider pawn. This is the classic "you ARE the vehicle now" model and needs the rideable to be a
	 * APawn with its own movement/input. Falls back to AttachOnly if the rideable is not a Pawn.
	 */
	Possess,

	/**
	 * Attach-only: the rider pawn stays possessed and is parented to the seat socket; the ride is moved
	 * by some other authority (AI, another driver, scripted). Used for passengers and for rides the
	 * player does not directly drive. No possession change occurs.
	 */
	AttachOnly
};

/** Result of an enter/exit request, surfaced to UI/AI so a failure can be explained. */
UENUM(BlueprintType)
enum class EWS_RideRequestResult : uint8
{
	Success,
	NoRideable,        // target had no UWS_RideableComponent
	NoFreeSeat,        // ride was full / requested seat taken
	TooFar,            // proximity check failed
	AlreadyAboard,     // this rider is already on a ride
	InvalidState       // missing pawn/controller/authority preconditions
};

/** Fired locally on the rider when it successfully boards a ride (server and owning client). */
DECLARE_MULTICAST_DELEGATE_TwoParams(FWS_OnEnteredRide, UWS_RideableComponent* /*Rideable*/, int32 /*SeatIndex*/);

/** Fired locally on the rider when it leaves a ride (server and owning client). */
DECLARE_MULTICAST_DELEGATE_OneParam(FWS_OnExitedRide, UWS_RideableComponent* /*Rideable*/);

/**
 * UActorComponent placed on the PLAYER-OWNED pawn that wants to mount/dismount rideables. It is the
 * only client->server entry point for entering and leaving a ride; the authoritative seat table lives
 * on the rideable's UWS_RideableComponent.
 *
 * Flow (client press "mount"):
 *   1. Client calls RequestEnter(Rideable, SeatIndex). This does NOT mutate anything authoritative; it
 *      forwards to Server_Enter (Reliable, WithValidation).
 *   2. Server_Enter_Validate sanity-checks the arguments cheaply (non-null target with a rideable
 *      component, sane seat index, the calling pawn matches this component's owner).
 *   3. Server_Enter_Implementation RE-DERIVES the decision authoritatively: proximity, free-seat
 *      selection, then OccupySeat on the rideable, then attaches the pawn to the seat socket and — for a
 *      driver seat — performs the possession hand-off (or input attach), all guarded by HasAuthority().
 *
 * Possession-handoff approach (documented per the brief):
 *   The rider does NOT reinvent vehicle physics. For a driver seat in EWS_DriverControlMode::Possess,
 *   the server takes the rider's AController, remembers the original rider pawn, un-possesses it, and
 *   possesses the rideable actor (cast to APawn). Control then flows through the rideable's own movement
 *   component / input — i.e. the existing engine possession + input pipeline drives the ride. On exit
 *   the server re-possesses the remembered pawn and detaches. For EWS_DriverControlMode::AttachOnly (and
 *   for passenger seats) NO possession change happens: the pawn is simply attached to the seat socket and
 *   continues to be possessed by its own controller; input forwarding to the ride, if any, is the ride's
 *   responsibility (this component does not forge a second input chain). If Possess is requested but the
 *   rideable is not a APawn, the server logs and degrades to AttachOnly (documented inert default).
 *
 * All authoritative mutation is server-side and HasAuthority()-guarded; the client only requests.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent), HideCategories = (Variable, Sockets, Tags, ComponentReplication, Activation, Cooking, AssetUserData, Collision))
class DESIGNPATTERNSWORLDSYSTEMS_API UWS_RiderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWS_RiderComponent();

	//~ Begin UActorComponent
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	// ---- Client-side request API (call on the owning client) -----------------------------------

	/**
	 * Request to enter Rideable at PreferredSeatIndex. If PreferredSeatIndex is INDEX_NONE the server
	 * picks the best free seat (driver-first for a lone rider). Locally validates only that we have an
	 * owner/controller and are not already aboard, then forwards to the server. The real decision is the
	 * server's. Safe to call on the server too (skips the RPC and runs the authoritative path directly).
	 * @return false only if the call could not even be dispatched (no controller / already aboard).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|World|Ride")
	bool RequestEnter(UWS_RideableComponent* Rideable, int32 PreferredSeatIndex = INDEX_NONE);

	/** Request to exit the current ride. Forwards to the server; the server performs the dismount. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|World|Ride")
	bool RequestExit();

	// ---- Read-side ------------------------------------------------------------------------------

	/** The rideable this rider is currently on (server- and owner-relevant), or nullptr if on foot. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|World|Ride")
	UWS_RideableComponent* GetCurrentRideable() const { return CurrentRideable.Get(); }

	/** True if this rider is currently aboard a ride. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|World|Ride")
	bool IsRiding() const { return CurrentRideable.IsValid(); }

	/** Seat index this rider currently occupies, or INDEX_NONE if on foot. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|World|Ride")
	int32 GetCurrentSeatIndex() const { return CurrentSeatIndex; }

	// ---- Notifications --------------------------------------------------------------------------

	/** Fired when this rider boards a ride (server + owning client, after the OnRep/authority apply). */
	FWS_OnEnteredRide OnEnteredRide;

	/** Fired when this rider leaves a ride. */
	FWS_OnExitedRide OnExitedRide;

protected:
	/**
	 * Server RPC: validated request to enter Rideable at PreferredSeatIndex. Reliable so a mount intent is
	 * not dropped. The server re-derives proximity / seat selection and performs the authoritative mount.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Enter(UWS_RideableComponent* Rideable, int32 PreferredSeatIndex);

	/** Server RPC: validated request to exit the current ride. */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_Exit();

	/**
	 * Replicated-to-owner mirror of the ride this client is on, so the owning client's UI/camera can react
	 * without the rideable being independently relevant. ReplicatedUsing keeps the local enter/exit
	 * notifications firing on the owning client. Authoritative writes happen only inside the server path.
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentRideable)
	TWeakObjectPtr<UWS_RideableComponent> CurrentRideable;

	/** Replicated-to-owner seat index paired with CurrentRideable. INDEX_NONE when on foot. */
	UPROPERTY(Replicated)
	int32 CurrentSeatIndex = INDEX_NONE;

	/** OnRep for CurrentRideable: fire the enter/exit notification on the owning client. */
	UFUNCTION()
	void OnRep_CurrentRideable(const TWeakObjectPtr<UWS_RideableComponent>& OldRideable);

	// ---- Tunables -------------------------------------------------------------------------------

	/**
	 * How the driver of a ride gains control when THIS rider takes a driver seat. See EWS_DriverControlMode.
	 * Defaults to Possess (the rider becomes the vehicle); passenger seats ignore this and never possess.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|World|Ride")
	EWS_DriverControlMode DriverControlMode = EWS_DriverControlMode::Possess;

	/**
	 * Extra slack (cm) added to the sum of the rider's and rideable's approximate radii when the server
	 * checks mount proximity. Tunable so different content can require a closer/looser approach. A value
	 * of 0 means "must overlap the radii"; the default gives a forgiving arm's-reach. Clamped non-negative.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|World|Ride", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1000.0"))
	float MountReachSlack = 150.f;

private:
	/**
	 * Authoritative mount. Server-only; guards HasAuthority() at the top. Validates proximity, resolves a
	 * free seat, occupies it on the rideable, attaches the pawn to the seat socket, and (driver seat) does
	 * the possession hand-off / input routing. Updates the owner-replicated mirror. Returns the result.
	 */
	EWS_RideRequestResult AuthEnter(UWS_RideableComponent* Rideable, int32 PreferredSeatIndex);

	/** Authoritative dismount. Server-only; guards HasAuthority(). Reverses attach/possession and frees the seat. */
	EWS_RideRequestResult AuthExit();

	/**
	 * Server-side: place the rider pawn at SeatIndex's socket and, for a driver seat, hand off control.
	 * Returns false if a required precondition (pawn/socket) was missing.
	 */
	bool ApplySeating(UWS_RideableComponent* Rideable, int32 SeatIndex);

	/** Server-side: undo ApplySeating (detach, restore possession). */
	void ReleaseSeating(UWS_RideableComponent* Rideable, int32 SeatIndex);

	/** Approximate world radius (cm) of an actor, from its bounds, for the proximity check. */
	static float ApproxRadius(const AActor* Actor);

	/** The controller of this component's owning pawn, or nullptr. */
	AController* GetOwnerController() const;

	/** The owning pawn (this component lives on a pawn), or nullptr. */
	APawn* GetOwnerPawn() const;

	/**
	 * On a Possess driver hand-off the original rider pawn must be remembered so exit can re-possess it.
	 * Weak: if the original pawn is destroyed while riding we simply have nothing to return to. Server-only
	 * state; not replicated (the client does not perform possession).
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<APawn> PossessReturnPawn;

	/** True (server-only) while a Possess hand-off is active, so exit knows to reverse it. */
	bool bDidPossessHandoff = false;
};
