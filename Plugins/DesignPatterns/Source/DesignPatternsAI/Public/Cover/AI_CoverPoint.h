// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI_CoverPoint.generated.h"

/**
 * Fired (server and clients) when this cover point's claim state changes (after replication on clients).
 * @param Point     The cover point whose claim changed.
 * @param Claimant  The new claimant id (invalid when the cover was released).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAI_OnCoverClaimChanged,
	class AAI_CoverPoint*, Point, FSeam_EntityId, Claimant);

/**
 * Replicated cover MARKER: pure state, no behaviour — mirroring AAI_SquadCarrier.
 *
 * A cover point may be LEVEL-PLACED (a designer drops it next to geometry) OR authority-SPAWNED by
 * UAI_CoverSubsystem; either way it replicates with DORM_Initial + WakeForChange() on claim so a static
 * cover field costs no per-frame bandwidth. It is NEVER client-spawned (claims are authoritative).
 *
 * The point exposes its stable id (FSeam_EntityId), a cover-type tag, the directions it shields against,
 * and a single replicated Claimant. TryClaim/Release are AUTHORITY-ONLY and guard HasAuthority() at the
 * TOP. Consumers that only need to *read* cover go through ISeam_CoverProvider and never touch this type.
 */
UCLASS()
class DESIGNPATTERNSAI_API AAI_CoverPoint : public AInfo
{
	GENERATED_BODY()

public:
	AAI_CoverPoint();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	//~ End AActor

	// ---- Identity (assigned once on the authority right after spawn / on a placed point in BeginPlay) ----

	/**
	 * Ensure this point has a stable id. AUTHORITY ONLY. Generates one if unset; idempotent. Called by the
	 * subsystem right after spawning a point, and by a level-placed point's authority BeginPlay.
	 */
	void EnsureCoverId();

	/** This point's stable id (replicated). Invalid until EnsureCoverId runs on the authority. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Cover")
	FSeam_EntityId GetCoverId() const { return CoverId; }

	// ---- Authority mutators (each early-returns on clients) ----

	/**
	 * Claim this point for By if it is currently unclaimed (or already By's). AUTHORITY ONLY.
	 * @return true if the point is now claimed by By.
	 */
	bool TryClaim(const FSeam_EntityId& By);

	/** Release this point if currently claimed by By. AUTHORITY ONLY. */
	void Release(const FSeam_EntityId& By);

	// ---- Reads (client-safe) ----

	/** True if currently claimed by anyone. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Cover")
	bool IsClaimed() const { return Claimant.IsValid(); }

	/** The current claimant id, or invalid if free. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Cover")
	FSeam_EntityId GetClaimant() const { return Claimant; }

	/** The cover-type/classification tag (e.g. AI.Cover.Full). May be empty. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Cover")
	FGameplayTag GetCoverTypeTag() const { return CoverTypeTag; }

	/** The world transform an agent should occupy when using this cover (the actor transform). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Cover")
	FTransform GetStandTransform() const { return GetActorTransform(); }

	/**
	 * @return true if this point shields against fire coming FROM ThreatWorldDir (a normalized world
	 * direction pointing from the cover toward the threat): some protected direction must align with it
	 * within MinDot. ProtectedDirections are authored in the point's local space.
	 */
	bool ProtectsAgainst(const FVector& ThreatWorldDir, float MinDot) const;

	/** Fired when the claim state changes (server + clients). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|AI|Cover")
	FAI_OnCoverClaimChanged OnCoverClaimChanged;

	// ---- Config (designer-authored; no magic gameplay numbers in code) ----

	/** Cover-type tag classifying this point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "DesignPatterns|AI|Cover")
	FGameplayTag CoverTypeTag;

	/**
	 * Local-space directions this point shields against (each points from the cover toward the danger it
	 * blocks). Composed with the actor rotation when evaluating ProtectsAgainst. Not replicated — authored
	 * data identical on every machine.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "DesignPatterns|AI|Cover")
	TArray<FVector> ProtectedDirections;

private:
	/** Replicated stable id. */
	UPROPERTY(Replicated)
	FSeam_EntityId CoverId;

	/** Replicated claimant; OnRep surfaces the change so clients fire OnCoverClaimChanged. */
	UPROPERTY(ReplicatedUsing = OnRep_Claimant)
	FSeam_EntityId Claimant;

	/** Client reaction to a replicated claim change: fire the delegate. */
	UFUNCTION()
	void OnRep_Claimant(FSeam_EntityId PreviousClaimant);

	/** Wake the actor from net dormancy so a just-changed claim delta replicates this frame. */
	void WakeForChange();
};
