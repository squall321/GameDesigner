// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "GameplayTagContainer.h"
#include "Engine/EngineBaseTypes.h"
#include "Flow_TravelCoordinator.generated.h"

class UFlow_GameFlowSubsystem;
class UFlow_CarryOverSaveGame;
class UDP_SaveGameSubsystem;
class UDP_MessageBusSubsystem;

/**
 * Orchestrates carry-over save/restore AROUND the flow subsystem's existing level travel (it does NOT
 * reinvent travel — the engine's OpenLevel/ClientTravel still does the work). Owned as a
 * UPROPERTY(Transient) subobject of UFlow_GameFlowSubsystem (NewObject(Outer)); GameInstance-lifetime so
 * it survives the travel it coordinates.
 *
 * Pre-travel (PrepareTravel, called by the subsystem just before it travels for a phase):
 *  - gathers every registered carry-over participant (ISeam_Persistable, discovered on the bus-registered
 *    carry-over set) via CaptureState into a UFlow_CarryOverSaveGame keyed by GetPersistenceKind(),
 *  - writes that object through the EXISTING UDP_SaveGameSubsystem::SaveNow to the configured carry-over
 *    slot (the byte blob round-trips verbatim across the travel boundary),
 *  - announces DP.Bus.Flow.TravelStarted.
 *
 * Post-travel (HandlePostLoadMap, wired to PostLoadMapWithWorld): loads the carry-over slot back and
 * scatters each record to the matching participant in the NEW world via RestoreState (authority-guarded
 * inside each participant — a client-side restore is a no-op).
 *
 * Travel-failure recovery: registered on GEngine->OnTravelFailure / OnNetworkFailure; on a failure it
 * announces DP.Bus.Flow.TravelFailed and forces the flow into the configured NetErrorPhase.
 *
 * Participants opt into carry-over by registering a TScriptInterface<ISeam_Persistable> with this
 * coordinator (RegisterCarryOverParticipant) — typically from their own BeginPlay — so the coordinator
 * never has to scan the world for persistables. References are weak so a destroyed participant prunes.
 */
UCLASS()
class DESIGNPATTERNSGAMEFLOW_API UFlow_TravelCoordinator : public UObject
{
	GENERATED_BODY()

public:
	/** Bind to the owning flow subsystem and register engine travel-failure delegates. */
	void Initialize(UFlow_GameFlowSubsystem* InOwner);

	/** Remove engine delegate registrations + clear participants (called from the owner's Deinitialize). */
	void Shutdown();

	/**
	 * Register an object implementing ISeam_Persistable as a carry-over participant. The object is held
	 * weakly; it is captured on the next PrepareTravel and restored after the next travel. Safe to call
	 * with an already-registered participant (deduped).
	 */
	UFUNCTION(BlueprintCallable, Category = "Flow|Travel")
	void RegisterCarryOverParticipant(UObject* Participant);

	/** Unregister a carry-over participant. */
	UFUNCTION(BlueprintCallable, Category = "Flow|Travel")
	void UnregisterCarryOverParticipant(UObject* Participant);

	/**
	 * Capture carry-over from all participants and write it to the carry-over slot. Called by the subsystem
	 * immediately BEFORE it travels for TargetPhase. Also announces TravelStarted. bSeamless tells listeners
	 * whether the upcoming travel is seamless/relative (false = absolute/OpenLevel).
	 */
	void PrepareTravel(FGameplayTag TargetPhase, bool bSeamless);

	/** True if a carry-over restore is pending (set by PrepareTravel, cleared after RestoreCarryOver). */
	bool IsRestorePending() const { return bRestorePending; }

private:
	/** Gather all participants' state into a fresh carry-over save object. */
	UFlow_CarryOverSaveGame* CaptureCarryOver();

	/** Load the carry-over slot and scatter records back to live participants. */
	void RestoreCarryOver();

	/** PostLoadMapWithWorld handler: trigger a pending restore once the new world is in. */
	void HandlePostLoadMap(UWorld* LoadedWorld);

	/** GEngine travel-failure handler. */
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	/** GEngine network-failure handler (also routes to NetError recovery). */
	void HandleNetworkFailure(UWorld* World, class UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);

	/** Force the flow into NetErrorPhase and announce TravelFailed. */
	void EnterTravelError(const FString& Reason);

	/** Resolve the core save subsystem (per-use; never cached across travel). */
	UDP_SaveGameSubsystem* GetSaveSubsystem() const;

	/** Resolve the owning GameInstance message bus, or null. */
	UDP_MessageBusSubsystem* GetBus() const;

	/** Prune dead weak participants. */
	void PruneParticipants();

	// --- State ---

	/** Owning flow subsystem. */
	UPROPERTY(Transient)
	TWeakObjectPtr<UFlow_GameFlowSubsystem> Owner;

	/** Weakly-held carry-over participants (objects implementing ISeam_Persistable). */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UObject>> Participants;

	/** The phase the in-flight travel is heading to. */
	FGameplayTag PendingTargetPhase;

	/** True between PrepareTravel and the post-travel restore. */
	bool bRestorePending = false;

	/** Engine delegate handles, removed on Shutdown. */
	FDelegateHandle PostLoadMapHandle;
	FDelegateHandle TravelFailureHandle;
	FDelegateHandle NetworkFailureHandle;
};
