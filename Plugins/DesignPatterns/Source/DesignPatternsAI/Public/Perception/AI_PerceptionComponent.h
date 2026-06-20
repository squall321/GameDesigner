// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI_PerceptionComponent.generated.h"

// Engine AIModule types are a PRIVATE dependency of this module; forward-declare here and include
// "Perception/AIPerceptionComponent.h" only in the .cpp so PUBLIC consumers never inherit AIModule.
class UAIPerceptionComponent;
class AActor;

// Core blackboard provider seam (FName-keyed) — included for the optional push target.
class IDP_BlackboardProvider;

/**
 * One normalized percept: the AI module's tag-keyed view of an engine AI stimulus.
 *
 * The engine reports stimuli as sense-class + strength + location on a UAIPerceptionComponent;
 * this struct re-expresses that as an FGameplayTag-keyed record (AI.Percept.Sight / .Hearing /
 * .Damage) with a stable FSeam_EntityId for the perceived actor, so the rest of the game can reason
 * about "who do I sense and how" without touching AIModule. NOT replicated (sensing is authority-
 * driven and local to the server; results feed already-replicated gameplay or the world hub).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_Percept
{
	GENERATED_BODY()

	/** Which sense produced this percept (AI.Percept.Sight / .Hearing / .Damage). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	FGameplayTag SenseTag;

	/** Stable id of the perceived entity (invalid if the source actor has no identity seam). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	FSeam_EntityId SourceId;

	/** The perceived actor. Weak: a destroyed source must not keep this alive. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	TWeakObjectPtr<AActor> SourceActor;

	/** True while the stimulus is actively sensed; false on the frame it is lost/forgotten. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	bool bSensed = false;

	/** Last known world-space location reported for the stimulus. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	FVector LastKnownLocation = FVector::ZeroVector;

	/** Normalized stimulus strength in [0,1] (engine strength clamped). */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	float Strength = 0.f;

	/** Game-time seconds (world TimeSeconds) at which this percept was last updated. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	double UpdatedAtTime = 0.0;

	FAI_Percept() = default;
};

/**
 * Flat, weak-ref-free bus payload broadcast on DP.Bus.AI.PerceptUpdated when a percept changes.
 *
 * Carries only net/save-safe fields (no UObject or weak pointers) so it is safe to flatten into an
 * FInstancedStruct and queue for deferred dispatch on the local message bus.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_PerceptUpdatedPayload
{
	GENERATED_BODY()

	/** Stable id of the agent that did the sensing. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	FSeam_EntityId SensingAgentId;

	/** Stable id of the perceived entity. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	FSeam_EntityId SourceId;

	/** Which sense produced the percept. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	FGameplayTag SenseTag;

	/** True if the stimulus is now sensed; false if it was lost. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	bool bSensed = false;

	/** Last known location of the stimulus. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatternsAI|Perception")
	FVector LastKnownLocation = FVector::ZeroVector;

	FAI_PerceptUpdatedPayload() = default;
};

/**
 * Fired locally whenever a percept is gained, updated or lost.
 * @param Percept the normalized percept record (bSensed=false signals a lost stimulus).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAI_OnPerceptUpdated, const FAI_Percept&, Percept);

/**
 * Perception ADAPTER: a clean tag/data API over the engine's UAIPerceptionComponent.
 *
 * WRAP, DON'T REINVENT: this component owns an instanced engine UAIPerceptionComponent (the actual
 * sensing machinery), subscribes to its OnTargetPerceptionUpdated delegate, and NORMALIZES each
 * sight/hearing/damage stimulus into an FGameplayTag-keyed FAI_Percept. It then:
 *   - caches the live percept set (queryable on the server),
 *   - pushes a compact summary into the owner's IDP_BlackboardProvider (resolved off the owner) so
 *     FSM guards / strategies can read "do I see a target / how strong", and
 *   - optionally writes presence into the World hub (resolved via the service locator in .cpp),
 *   - broadcasts DP.Bus.AI.PerceptUpdated on the core message bus.
 *
 * AUTHORITY: sensing is authority-driven (the engine perception system runs server-side); this
 * component only does work on the server / standalone and leaves clients to react to already-
 * replicated gameplay. Nothing here replicates directly (cosmetic/local view of authoritative AI).
 */
UCLASS(ClassGroup = (DesignPatternsAI), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSAI_API UAI_PerceptionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAI_PerceptionComponent();

	//~ Begin UActorComponent
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/** Fired locally whenever a percept is gained, updated or lost. */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatternsAI|Perception")
	FAI_OnPerceptUpdated OnPerceptUpdated;

	/**
	 * @return the currently-sensed percepts (only entries with bSensed=true) by value.
	 * Server-authoritative; on clients this is typically empty (sensing runs on the server).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsAI|Perception")
	TArray<FAI_Percept> GetActivePercepts() const;

	/** @return the strongest currently-sensed percept matching SenseTag (or any sense if invalid). */
	UFUNCTION(BlueprintCallable, Category = "DesignPatternsAI|Perception")
	bool GetStrongestPercept(FGameplayTag SenseTag, FAI_Percept& OutPercept) const;

	/** @return true if anything is currently sensed for SenseTag (or any sense if SenseTag invalid). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsAI|Perception")
	bool HasPerceptFor(FGameplayTag SenseTag) const;

	/** @return the underlying engine perception component this adapter wraps (may be null pre-register). */
	UFUNCTION(BlueprintPure, Category = "DesignPatternsAI|Perception")
	UAIPerceptionComponent* GetInnerPerceptionComponent() const { return InnerPerception; }

	// ---- Config (tunables; no magic numbers in code) ----

	/**
	 * When true, every percept update pushes a compact summary into the owner's blackboard
	 * (resolved via IDP_BlackboardProvider on the owner or one of its components).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Perception")
	bool bPushToBlackboard = true;

	/**
	 * When true, percept presence is also written into the World hub (entity-scoped flag) so other
	 * server systems can query "does agent X currently sense anything" without a direct dependency.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Perception")
	bool bPushToWorldHub = false;

	/** When true, percept updates are republished on the core message bus (DP.Bus.AI.PerceptUpdated). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Perception")
	bool bBroadcastOnBus = true;

	/** Blackboard key written with whether ANY target is currently sensed (bool). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Perception|Blackboard")
	FName BlackboardKey_HasTarget = TEXT("AI.HasTarget");

	/** Blackboard key written with the last known target location (vector). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Perception|Blackboard")
	FName BlackboardKey_TargetLocation = TEXT("AI.TargetLocation");

	/** Blackboard key written with the perceived target actor (object). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Perception|Blackboard")
	FName BlackboardKey_TargetActor = TEXT("AI.TargetActor");

	/**
	 * World-hub flag key written (entity-scoped) with whether the agent currently senses anything.
	 * Only used when bPushToWorldHub is true.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatternsAI|Perception|WorldHub")
	FGameplayTag WorldHubKey_HasTarget;

protected:
	/**
	 * Engine sensing machinery this adapter wraps. Created in OnRegister and registered as a
	 * subobject of this component so GC owns it. We subscribe to its perception-updated delegate.
	 */
	UPROPERTY(VisibleAnywhere, Instanced, Category = "DesignPatternsAI|Perception")
	TObjectPtr<UAIPerceptionComponent> InnerPerception;

	/** Live percept cache keyed by perceived actor; only sensed entries are kept queryable. */
	UPROPERTY(VisibleInstanceOnly, Transient, Category = "DesignPatternsAI|Perception")
	TArray<FAI_Percept> Percepts;

	/**
	 * Tiny UObject that owns the UFUNCTION bound to the engine's dynamic perception delegate and
	 * forwards back to this adapter. It lives here (rather than putting an engine-struct UFUNCTION
	 * on this component) so the AIModule type FAIStimulus stays OUT of this public header — keeping
	 * AIModule a private dependency. Created in OnRegister; type is fully defined in the .cpp.
	 */
	UPROPERTY(Transient)
	TObjectPtr<UObject> PerceptionListener;

public:
	/**
	 * Ingest one engine stimulus update, called by the private listener. Public so the .cpp-local
	 * listener UObject can forward to it; takes only normalized, engine-free parameters.
	 * @param Actor             the perceived actor (may be null/forgotten).
	 * @param SenseTag          the AI.Percept.* tag classifying the producing sense.
	 * @param bSensed           true if currently sensed, false if lost/forgotten.
	 * @param StimulusLocation  last known world-space stimulus location.
	 * @param NormalizedStrength stimulus strength clamped to [0,1].
	 */
	void IngestStimulus(AActor* Actor, FGameplayTag SenseTag, bool bSensed, const FVector& StimulusLocation, float NormalizedStrength);

private:
	/** Resolve the owner's stable entity id via the identity seam (invalid if none). */
	FSeam_EntityId GetOwnerEntityId() const;

	/** Resolve a stable entity id for a perceived actor via the identity seam (invalid if none). */
	static FSeam_EntityId GetEntityIdFor(const AActor* Actor);

	/** Find the owner's IDP_BlackboardProvider (on the owner actor or one of its components). */
	IDP_BlackboardProvider* ResolveBlackboardProvider() const;

	/** Insert/update/remove a percept in the cache and run all push/broadcast side effects. */
	void ApplyPercept(const FAI_Percept& Percept);

	/** Recompute and push the compact summary (HasTarget/Location/Actor) to the blackboard + hub. */
	void PushSummary();

	/** Broadcast a flat percept-updated payload on the core message bus. */
	void BroadcastPerceptOnBus(const FAI_Percept& Percept) const;

	/** True only if we own an actor and that actor has network authority (server/standalone). */
	bool HasAuthoritySafe() const;
};
