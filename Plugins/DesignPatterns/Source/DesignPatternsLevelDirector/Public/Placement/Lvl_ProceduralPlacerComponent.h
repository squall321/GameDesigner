// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Placement/Lvl_PlacementTypes.h"
#include "Lvl_ProceduralPlacerComponent.generated.h"

class ULvl_PlacementRuleSet;
class UDP_ServiceLocatorSubsystem;
class UDP_SpawnFactorySubsystem;
class AActor;
struct FHitResult;

/**
 * AUTHORITY-ONLY procedural scatter/placement component.
 *
 * Drives one deterministic placement pass from a ULvl_PlacementRuleSet:
 *   1. Gates the whole pass on ISeam_ActivationGate (resolved from the service locator; DEFAULT OPEN
 *      when the gate seam is unresolved, so a project without the World hub still places content).
 *   2. Seeds an FRandomStream from the rule set's RandomSeed (or the owner-name hash when 0) so the
 *      layout is reproducible across runs and across save/load.
 *   3. Generates candidate positions (box / radial / spline) per the rule set's density.
 *   4. Validates each candidate against the read-only grid seam (ISeam_TileProviderRead) tile masks,
 *      then projects it onto the world surface with a downward trace (the tile seam carries no height),
 *      applying slope and spacing rules.
 *   5. Spawns each accepted actor through the core UDP_SpawnFactorySubsystem (which itself routes
 *      through the object pool when the recipe permits) — falling back to a direct soft-class spawn
 *      only when no factory is registered for the identity tag.
 *   6. Records every result into an FLvl_PlacementManifest for save/restore.
 *
 * REPLICATION: the component does NOT replicate the manifest. Spawning is server-authoritative and the
 * spawned actors replicate themselves normally; clients learn of the pass from those actors and from
 * the locally-republished DP.Bus.Lvl.Placement.* message. GeneratePlacement / ClearPlacement are
 * authority-guarded at the TOP and are no-ops on clients.
 *
 * REMOVABILITY: with the activation-gate seam unresolved the pass runs (gate open); with the tile seam
 * unresolved tile-mask validation is skipped (candidates pass the mask, the surface trace still gates
 * height) — so the component composes whether or not SimGrid / the World hub are present.
 */
UCLASS(ClassGroup = "DesignPatterns|LevelDirector", meta = (BlueprintSpawnableComponent),
	HideCategories = ("ComponentReplication", "Cooking", "AssetUserData"))
class DESIGNPATTERNSLEVELDIRECTOR_API ULvl_ProceduralPlacerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	ULvl_ProceduralPlacerComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	/**
	 * UActorComponent has no HasWorldAuthority(); derive one. True on server / standalone /
	 * listen-server host. ALL placement gates on this at the TOP.
	 */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Placement")
	bool HasWorldAuthority() const;

	// ---- Configuration --------------------------------------------------------------------------

	/** The rule set this placer executes. Required for a pass to run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Placement")
	TObjectPtr<ULvl_PlacementRuleSet> RuleSet;

	/**
	 * Optional region tag override. When set it replaces the rule set's DefaultRegionTag in the
	 * manifest (so several placers can share a rule set yet save independently).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Placement")
	FGameplayTag RegionTagOverride;

	/**
	 * Optional seed override. >= 0 replaces the rule set's RandomSeed for THIS placer (still
	 * deterministic). -1 (default) means "use the rule set's seed policy".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Placement")
	int32 SeedOverride = -1;

	/** If true, the placer runs GeneratePlacement automatically on BeginPlay (authority only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Placement")
	bool bGenerateOnBeginPlay = false;

	/**
	 * If true, ClearPlacement destroys the actors it previously spawned on EndPlay. If false, placed
	 * actors are left in the world (e.g. when the level itself owns their lifetime).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DesignPatterns|Lvl|Placement")
	bool bClearOnEndPlay = true;

	// ---- Pass control (AUTHORITY ONLY) ----------------------------------------------------------

	/**
	 * Run the placement pass. Authority-guarded at the top (no-op on clients). If the activation gate
	 * is closed, the pass produces nothing and returns false. Re-running first clears the previous
	 * pass. @return true if the pass ran and placed at least one actor.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Placement")
	bool GeneratePlacement();

	/**
	 * Tear down the current pass: destroys this placer's spawned actors (those still valid) and
	 * empties the manifest. Authority-guarded at the top (no-op on clients).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Placement")
	void ClearPlacement();

	/**
	 * Restore a previously-captured manifest by re-spawning its entries deterministically. Used by the
	 * save path. Authority-guarded at the top (client-side restore is a no-op). The stored seed is
	 * adopted so any regeneration matches the original. @return number of actors re-spawned.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|Lvl|Placement")
	int32 RestoreFromManifest(const FLvl_PlacementManifest& InManifest);

	/** The manifest of the current pass (empty before GeneratePlacement / after ClearPlacement). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Placement")
	const FLvl_PlacementManifest& GetManifest() const { return Manifest; }

	/** True while a pass has placed entries recorded. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Placement")
	bool HasPlacement() const { return Manifest.HasEntries(); }

	/** The effective region tag (override if set, else the rule set's default). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Placement")
	FGameplayTag GetEffectiveRegionTag() const;

	/** The effective seed (override if >= 0, else the rule set's seed policy resolved to a concrete int). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|Lvl|Placement")
	int32 GetEffectiveSeed() const;

private:
	/** The manifest produced by the current pass. NOT replicated; rebuilt authority-side and saved. */
	UPROPERTY(Transient)
	FLvl_PlacementManifest Manifest;

	/**
	 * Actors this placer spawned this session, kept so ClearPlacement can destroy exactly its own
	 * output. Weak (non-owning): the world owns the actors; pruned on use.
	 */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AActor>> SpawnedActors;

	// ---- Internals ------------------------------------------------------------------------------

	/** Resolve the GameInstance service locator (null-safe). */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/** Resolve the world spawn factory (null-safe). */
	UDP_SpawnFactorySubsystem* GetFactory() const;

	/**
	 * Ask the activation gate seam whether GateKey is open. DEFAULT OPEN when the gate seam is
	 * unresolved or GateKey is invalid (an ungated pass).
	 */
	bool IsGateOpen(const FGameplayTag& GateKey) const;

	/**
	 * Resolve the tile provider seam, if any. Returns a non-null UObject implementing
	 * ISeam_TileProviderRead, or null when unresolved (then tile-mask validation is skipped).
	 */
	UObject* ResolveTileProvider() const;

	/** Generate the raw candidate world positions for the rule set's source. */
	void GenerateCandidates(const ULvl_PlacementRuleSet& Rules, FRandomStream& Stream,
		TArray<FVector>& OutCandidates) const;

	/** Box / radial candidate scatter in the owner's local space, transformed to world. */
	void GenerateAreaCandidates(const ULvl_PlacementRuleSet& Rules, FRandomStream& Stream,
		TArray<FVector>& OutCandidates) const;

	/** Spline candidate sampling along the owner's first spline component (if any). */
	void GenerateSplineCandidates(const ULvl_PlacementRuleSet& Rules, FRandomStream& Stream,
		TArray<FVector>& OutCandidates) const;

	/**
	 * Validate one candidate against the tile masks (via TileProvider) and the surface projection.
	 * On success fills OutTransform with the final placement transform (post-projection, with the
	 * choice's vertical offset, variation yaw/scale and optional surface alignment applied).
	 * @return true if the candidate is accepted.
	 */
	bool ValidateAndProject(const ULvl_PlacementRuleSet& Rules, const FLvl_PlacementClassChoice& Choice,
		FRandomStream& Stream, UObject* TileProvider, const FVector& Candidate, FTransform& OutTransform) const;

	/** True if Cell's tile-type tag satisfies the rule set's allow/block masks (via TileProvider). */
	bool PassesTileMask(const ULvl_PlacementRuleSet& Rules, UObject* TileProvider, const FVector& WorldLocation) const;

	/** Downward surface trace. Fills OutHit on a blocking hit; returns whether one occurred. */
	bool TraceSurface(const ULvl_PlacementRuleSet& Rules, const FVector& Candidate, FHitResult& OutHit) const;

	/**
	 * Spawn one actor for an accepted placement, preferring the core factory (pool-aware), falling
	 * back to a direct soft-class spawn. Returns the spawned actor or null.
	 */
	AActor* SpawnEntry(const ULvl_PlacementRuleSet& Rules, const FLvl_PlacementClassChoice& Choice,
		const FTransform& Transform);

	/**
	 * Spawn one entry from a SAVED manifest record (by class tag + stored transform). Used by restore;
	 * does NOT re-run validation (the transform was already validated at generate time).
	 */
	AActor* SpawnFromEntry(const FLvl_PlacedEntry& Entry);

	/** Build the deterministic per-placement GUID from the seed and candidate index. */
	static FGuid MakeDeterministicPlacementId(int32 Seed, int32 CandidateIndex);

	/** Broadcast a Generated / Cleared event on the message bus. */
	void BroadcastPlacementEvent(const FGameplayTag& Channel, int32 PlacedCount) const;

	/** Destroy any still-valid actors this placer spawned and clear the tracking array. */
	void DestroySpawnedActors();
};
