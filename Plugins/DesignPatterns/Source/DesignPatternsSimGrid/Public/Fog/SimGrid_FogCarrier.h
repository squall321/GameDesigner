// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"
#include "Fog/SimGrid_FogTypes.h"
#include "SimGrid_FogCarrier.generated.h"

/**
 * Replicated authority carrier for one team's fog-of-war state.
 *
 * One ASimGrid_FogCarrier is spawned per team.  All authoritative fog state
 * lives here, mirroring ASimGrid_ZoneCarrier for zones and
 * ASimGrid_ChunkReplicator for tiles.  The carrier delta-replicates two
 * fast arrays to clients:
 *
 *   ExploredRuns — every cell the team has ever seen (permanent; never cleared).
 *   VisibleRuns  — cells currently in line-of-sight (ephemeral; cleared by
 *                  ConcealAll / ConcealRadius; populated only when
 *                  USimGrid_FeatureSettings::bTrackCurrentVisibility is true).
 *
 * RELEVANCY: bOnlyRelevantToOwner=true.  The GameMode is expected to call
 * SetOwner() to a controller belonging to the appropriate team, so only that
 * team's clients receive this carrier's replicated data.
 *
 * AUTHORITY: every state mutator checks HasAuthority() at the very top and is a
 * no-op on clients.  Clients receive fog deltas via FFastArraySerializer and
 * observe changes through OnFogChanged.
 *
 * SERVICE REGISTRATION: BeginPlay registers self in the service locator under
 * USimGrid_FeatureSettings::FogCarrierServiceTag (falling back to
 * SimGridTags::Service_FogCarrier).  Because the locator holds one entry per
 * tag, in single-team worlds Resolve() is sufficient.  In multi-team worlds
 * use ResolveForTeam() which iterates world actors and filters by TeamId.
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API ASimGrid_FogCarrier : public AInfo
{
    GENERATED_BODY()

public:
    ASimGrid_FogCarrier();

    //~ Begin AActor
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    //~ End AActor

    // ─── Authority mutators ──────────────────────────────────────────────────
    // All functions below are AUTHORITY ONLY — each checks HasAuthority() at the
    // top and returns immediately if called on a client.

    /**
     * Reveal all cells within a square of half-side RadiusCells centred on Centre
     * for this team.  Adds entries to ExploredRuns (permanent) and, when
     * USimGrid_FeatureSettings::bTrackCurrentVisibility is true, to VisibleRuns
     * (ephemeral).  Radius is capped to
     * USimGrid_FeatureSettings::GetSafeMaxFogRevealRadius().  AUTHORITY ONLY.
     */
    UFUNCTION(BlueprintCallable, Category = "SimGrid|Fog")
    void RevealRadius(const FSeam_CellCoord& Centre, int32 RadiusCells);

    /**
     * Mark all cells within a square of half-side RadiusCells centred on Centre
     * as explored-but-not-visible (dim / shroud).  Only VisibleRuns is modified;
     * ExploredRuns are permanent.  AUTHORITY ONLY.
     */
    UFUNCTION(BlueprintCallable, Category = "SimGrid|Fog")
    void ConcealRadius(const FSeam_CellCoord& Centre, int32 RadiusCells);

    /**
     * Conceal ALL currently-visible cells, e.g. at end of turn.  Clears
     * VisibleRuns entirely and marks the array dirty.  ExploredRuns are
     * unaffected.  AUTHORITY ONLY.
     */
    UFUNCTION(BlueprintCallable, Category = "SimGrid|Fog")
    void ConcealAll();

    // ─── Read queries (client-safe) ──────────────────────────────────────────

    /**
     * True if Cell has ever been explored (seen at any point in the game) by
     * this team.  Safe to call on both server and clients.
     */
    UFUNCTION(BlueprintPure, Category = "SimGrid|Fog")
    bool IsExplored(const FSeam_CellCoord& Cell) const;

    /**
     * True if Cell is currently visible to this team (not merely explored).
     * Always returns false when bTrackCurrentVisibility is disabled in settings.
     * Safe to call on both server and clients.
     */
    UFUNCTION(BlueprintPure, Category = "SimGrid|Fog")
    bool IsCurrentlyVisible(const FSeam_CellCoord& Cell) const;

    // ─── Static resolvers ────────────────────────────────────────────────────

    /**
     * Resolve the fog carrier from the service locator using the world context.
     * Returns null if no carrier has registered.  Suitable for single-team worlds.
     */
    static ASimGrid_FogCarrier* Resolve(const UObject* WorldContextObject);

    /**
     * Resolve the fog carrier for a specific TeamId by iterating all
     * ASimGrid_FogCarrier actors in the world and matching TeamId.  More
     * expensive than Resolve() but correct for multi-team worlds.
     * Returns null if no matching carrier is found.
     */
    static ASimGrid_FogCarrier* ResolveForTeam(const UObject* WorldContextObject,
                                                const FSeam_EntityId& TeamId);

    // ─── Replication notification hook ───────────────────────────────────────

    /**
     * Called by FSimGrid_FogRun fast-array callbacks (on clients) and also
     * directly by authority mutators (on the server) to broadcast OnFogChanged
     * for the given cell.
     */
    void HandleReplicatedFogChange(const FSeam_CellCoord& Cell);

    // ─── Public state ────────────────────────────────────────────────────────

    /**
     * Fired on both server and clients whenever a fog cell's state changes
     * (explore or visibility transition).
     */
    UPROPERTY(BlueprintAssignable, Category = "SimGrid|Fog")
    FSimGrid_OnFogChanged OnFogChanged;

    /**
     * Stable team identifier this carrier belongs to.  Set by the authority
     * immediately after spawning.  Replicated so clients know which team's fog
     * this carrier represents.
     */
    UPROPERTY(BlueprintReadOnly, Replicated, Category = "SimGrid|Fog")
    FSeam_EntityId TeamId;

private:
    /**
     * Every cell the team has ever explored (seen).  Permanent — entries are only
     * added, never removed.  Delta-replicated to clients via FFastArraySerializer.
     */
    UPROPERTY(Replicated)
    FSimGrid_FogRunArray ExploredRuns;

    /**
     * Cells currently visible to the team.  Ephemeral — cleared by ConcealAll or
     * ConcealRadius.  Populated only when
     * USimGrid_FeatureSettings::bTrackCurrentVisibility is true.
     * Delta-replicated to clients.
     */
    UPROPERTY(Replicated)
    FSimGrid_FogRunArray VisibleRuns;

    /** Service-locator key this carrier registered under; cached for clean unregister. */
    UPROPERTY(Transient)
    FGameplayTag RegisteredServiceTag;

    // ─── Internal helpers ────────────────────────────────────────────────────

    /** Register this carrier with the game-instance service locator. */
    void RegisterService();

    /** Unregister this carrier from the game-instance service locator. */
    void UnregisterService();

    /** Wake the actor from net dormancy so a just-changed delta replicates this frame. */
    void WakeForChange();

    /**
     * Linear search through RunArray.Entries for a run that covers Cell
     * (RowY == Cell.Y && StartX <= Cell.X && EndX >= Cell.X).
     */
    static bool RunArrayContains(const FSimGrid_FogRunArray& RunArray,
                                 const FSeam_CellCoord& Cell);

    /**
     * Ensure Cell is covered in RunArray with bCurrentlyVisible set.  If the cell
     * is not already present, inserts a new single-cell run and marks it dirty.
     * Returns true if a new run was inserted.
     */
    static bool EnsureCellInRunArray(FSimGrid_FogRunArray& RunArray,
                                     const FSeam_CellCoord& Cell,
                                     bool bCurrentlyVisible,
                                     ASimGrid_FogCarrier* Carrier);

    /**
     * Remove every run in RunArray that covers Cell exactly (StartX == EndX ==
     * Cell.X && RowY == Cell.Y).  Returns true if at least one run was removed.
     * Used by ConcealRadius to strip single-cell visible entries.
     */
    static bool RemoveCellFromRunArray(FSimGrid_FogRunArray& RunArray,
                                       const FSeam_CellCoord& Cell,
                                       ASimGrid_FogCarrier* Carrier);
};
