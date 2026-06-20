// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Engine/EngineTypes.h"
#include "Identity/Seam_EntityId.h"
#include "Seam/Cam_TargetSource.h"
#include "Targeting/Cam_TargetingTypes.h"
#include "Cam_TargetingComponent.generated.h"

class UCam_TargetSelectionStrategy;
class APlayerController;
class APlayerCameraManager;

/** Broadcast (locally) when the locked target changes — for HUD/reticle hookup in the same client. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FCam_OnTargetChanged, FSeam_EntityId, NewTargetId, FSeam_EntityId, PreviousTargetId);

/**
 * Player-owned lock-on / targeting component.
 *
 * Attach to the locally-controlled pawn (or player controller). It:
 *   1. GATHERS candidates with a sphere overlap within MaxTargetRange, then filters them to a cone
 *      (AcquisitionHalfAngleDeg) in front of the view, building FCam_TargetCandidate rows that read
 *      each candidate's stable id from the ISeam_EntityIdentity seam.
 *   2. SELECTS one via an inline UCam_TargetSelectionStrategy (closest / most-centered / highest-
 *      threat), reusing the core Strategy pattern — policy is data, not code.
 *   3. Exposes the chosen target by FSeam_EntityId through the ICam_TargetSource read seam (never a
 *      raw AActor*), and registers itself as the local target-source service so a HUD can resolve it.
 *   4. LOCK-ON: ToggleLock / CycleTarget(+1/-1) keep a hard lock and walk the sorted candidate ring.
 *
 * NET MODEL: pure camera FRAMING is LOCAL/COSMETIC and never replicated. BUT if the chosen target
 * feeds GAMEPLAY (soft-lock aim assist the server must honor), the id is sent to the server via
 * ServerSetSoftLockTarget_Validate/_Implementation — a client->server INTENT on this player-owned
 * component. The server validates the id is a real, in-range candidate before accepting it. We do
 * NOT replicate the cosmetic lock state back down; clients re-derive their own framing.
 */
UCLASS(ClassGroup = (DesignPatterns), meta = (BlueprintSpawnableComponent),
	HideCategories = ("Collision", "Cooking", "AssetUserData"))
class DESIGNPATTERNSCAMERA_API UCam_TargetingComponent : public UActorComponent, public ICam_TargetSource
{
	GENERATED_BODY()

public:
	UCam_TargetingComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent

	//~ Begin ICam_TargetSource
	virtual FSeam_EntityId GetCurrentTarget_Implementation() const override;
	virtual bool HasTarget_Implementation() const override;
	//~ End ICam_TargetSource

	/** Fires locally whenever the locked target id changes (including to/from Invalid). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|Camera|Targeting")
	FCam_OnTargetChanged OnTargetChanged;

	// ---- Lock-on control (call from input) ----

	/**
	 * Toggle hard lock-on. Acquiring picks the best current candidate via the strategy; releasing
	 * clears the lock (soft auto-acquire may still run if bSoftAcquireWhenUnlocked is set).
	 * @return the locked target id after the toggle (Invalid if released / nothing to lock).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Targeting")
	FSeam_EntityId ToggleLock();

	/** Acquire (or re-acquire) the best candidate as a hard lock. No-op if no candidate qualifies. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Targeting")
	FSeam_EntityId AcquireLock();

	/** Clear any hard lock. Cosmetic; safe to call when already unlocked. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Targeting")
	void ReleaseLock();

	/**
	 * While hard-locked, step to the next (+1) or previous (-1) candidate around the screen-sorted
	 * ring. No-op when not locked or fewer than two candidates exist.
	 * @return the newly-locked target id.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Camera|Targeting")
	FSeam_EntityId CycleTarget(int32 Direction);

	/** @return true while a hard lock is held. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Camera|Targeting")
	bool IsHardLocked() const { return bHardLocked; }

	/** Resolve the actor for the current target id this frame, or null. Convenience for local framing. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DesignPatterns|Camera|Targeting")
	AActor* GetCurrentTargetActor() const;

protected:
	// ---- Tunables (no magic numbers in code; <=0 means "use project default from settings") ----

	/** Max range (cm) to consider candidates. <= 0 = use UCam_DeveloperSettings::DefaultMaxTargetRange. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxTargetRange = 0.f;

	/** Half-angle (deg) of the acquisition cone. <= 0 = use settings default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float AcquisitionHalfAngleDeg = 0.f;

	/** Sphere radius (cm) of the overlap query gathering candidates. <= 0 = use settings default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float CandidateOverlapRadius = 0.f;

	/** Object types the overlap query collects (e.g. Pawn). Empty = Pawn + PhysicsBody defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting")
	TArray<TEnumAsByte<EObjectTypeQuery>> CandidateObjectTypes;

	/**
	 * Optional archetype filter: when set, only candidates whose ISeam_EntityIdentity archetype tag
	 * matches (hierarchy-aware) are eligible. Empty = accept any identified actor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (Categories = "DP"))
	FGameplayTag RequiredArchetypeTag;

	/**
	 * Require an unobstructed line of sight from the view location to the candidate focus before it
	 * is eligible. Uses a visibility trace. Off by default (cheaper); enable for "can't lock through walls".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting")
	bool bRequireLineOfSight = false;

	/** Trace channel used for the line-of-sight check when bRequireLineOfSight is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (EditCondition = "bRequireLineOfSight"))
	TEnumAsByte<ECollisionChannel> LineOfSightChannel = ECC_Visibility;

	/**
	 * The selection policy. Authored inline (EditInline on UDP_Strategy) so designers pick/replace
	 * closest / most-centered / highest-threat per project. When null, a built-in closest fallback runs.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "Camera|Targeting")
	TObjectPtr<UCam_TargetSelectionStrategy> SelectionStrategy;

	/**
	 * When true and not hard-locked, the component still auto-acquires the best candidate each tick
	 * (soft-lock) and exposes it via the seam — useful for aim assist / contextual reticles.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting")
	bool bSoftAcquireWhenUnlocked = true;

	/**
	 * When true the chosen target id is sent to the server (ServerSetSoftLockTarget) so server-side
	 * aim assist can honor it. Leave false for purely cosmetic framing. Only meaningful on owning clients.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting|Net")
	bool bReportTargetToServer = false;

	/** Seconds between candidate re-gathers while soft-acquiring (throttle). Hard lock re-validates every tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SoftAcquireInterval = 0.1f;

	/**
	 * Input-mode priority used when pushing the lock-on input mode through the shared arbiter while
	 * hard-locked, so the project's look-axis can be remapped to "cycle target".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Targeting", meta = (ClampMin = "0"))
	int32 LockOnInputModePriority = 100;

private:
	/** The currently-exposed target (hard-locked id while locked, else last soft-acquired id). */
	UPROPERTY(Transient)
	FSeam_EntityId CurrentTargetId;

	/** True while a hard lock is held. */
	UPROPERTY(Transient)
	bool bHardLocked = false;

	/**
	 * Server-accepted soft-lock target id (authority only). Set via ServerSetSoftLockTarget after
	 * validation; server aim-assist reads this. NOT replicated back (cosmetic framing stays local).
	 */
	UPROPERTY(Transient)
	FSeam_EntityId ServerAcceptedTargetId;

	/** Last id we reported to the server, to avoid spamming identical RPCs. */
	FSeam_EntityId LastReportedTargetId;

	/** Screen-sorted candidate id ring from the last gather, used by CycleTarget for stable ordering. */
	UPROPERTY(Transient)
	TArray<FSeam_EntityId> CandidateRing;

	/** Accumulator for SoftAcquireInterval throttling. */
	float TimeSinceLastGather = 0.f;

	/** Active input-mode request id from the arbiter while hard-locked (Invalid when not pushed). */
	FGuid InputModeRequestId;

	// ---- Internals ----

	/** Resolve effective range/angle/radius, substituting settings defaults for the <=0 sentinels. */
	void ResolveTunables(float& OutRange, float& OutHalfAngleDeg, float& OutOverlapRadius) const;

	/** Build the current view (eye location/forward + tunables + current target + blackboard). */
	bool BuildView(FCam_TargetingView& OutView) const;

	/** Gather + cone-filter + identity-resolve candidates into OutCandidates. @return count. */
	int32 GatherCandidates(const FCam_TargetingView& View, TArray<FCam_TargetCandidate>& OutCandidates) const;

	/** Score candidates with the strategy (or built-in closest) and return the best id (or Invalid). */
	FSeam_EntityId SelectBest(const TArray<FCam_TargetCandidate>& Candidates, const FCam_TargetingView& View) const;

	/** Rebuild CandidateRing as ids sorted left-to-right by signed screen angle, for stable cycling. */
	void RebuildCandidateRing(TArray<FCam_TargetCandidate>& Candidates, const FCam_TargetingView& View);

	/** Apply a new current target id: update state, fire delegate + bus, report to server if enabled. */
	void SetCurrentTarget(const FSeam_EntityId& NewId, bool bHardLock);

	/** Resolve the owning local player controller (owner is a PC or a possessed pawn). */
	APlayerController* ResolveOwningPlayerController() const;

	/** Read an actor's stable id + archetype via ISeam_EntityIdentity. @return true if it has identity. */
	bool ReadIdentity(AActor* Actor, FSeam_EntityId& OutId, FGameplayTag& OutArchetype) const;

	/** Register/unregister this component as the local ICam_TargetSource service. */
	void RegisterAsTargetSourceService();
	void UnregisterTargetSourceService();

	/** Resolve the shared ISeam_InputModeArbiter provider object from the locator (or null). */
	UObject* ResolveInputArbiterObject() const;

	/** Push/pop the lock-on input mode through the shared arbiter (resolved from the locator). */
	void PushLockOnInputMode();
	void PopLockOnInputMode();

	// ---- Net: client -> server intent ----

	/**
	 * Client->server: report the soft-lock/aim-assist target the player chose. Server validates the
	 * id is currently a real, in-range candidate before accepting it into ServerAcceptedTargetId.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerSetSoftLockTarget(FSeam_EntityId TargetId);
	bool ServerSetSoftLockTarget_Validate(FSeam_EntityId TargetId);
	void ServerSetSoftLockTarget_Implementation(FSeam_EntityId TargetId);
};
