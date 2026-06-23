// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"
#include "SimGrid_FogRevealComponent.generated.h"

/**
 * Player-owned component that reveals fog-of-war around its owner actor's current grid position,
 * either automatically on a configurable time interval or on demand via RevealAtCell().
 *
 * REPLICATION MODEL — "client intent -> server action":
 *   The component uses SetIsReplicatedByDefault(true) so it exists on the server.  On the owning
 *   client, auto-tick or a Blueprint call invokes RevealAtCell(), which:
 *     - On authority (server): resolves the fog carrier and calls RevealRadius directly.
 *     - On a client: fires ServerReveal (Server Reliable WithValidation) so the server performs
 *       the authoritative reveal.  The server validates radius and TeamId before executing.
 *       Clients never write fog state directly.
 *
 * ACTOR SETUP:
 *   Attach to any pawn / character that should reveal fog.  Assign TeamId to match the entity id
 *   used by the corresponding ASimGrid_FogCarrier.  The grid position is derived from the owner's
 *   world location via ISeam_TileProviderRead::WorldToCell each auto-reveal tick.
 */
UCLASS(ClassGroup = (SimGrid), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMGRID_API USimGrid_FogRevealComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    USimGrid_FogRevealComponent();

    //~ Begin UActorComponent
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
                               FActorComponentTickFunction* ThisTickFunction) override;
    //~ End UActorComponent

    // ─── Configuration ───────────────────────────────────────────────────────

    /**
     * Stable team entity id this component reveals fog for.  Must match the TeamId of the
     * ASimGrid_FogCarrier belonging to the owning player's team.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Fog")
    FSeam_EntityId TeamId;

    /**
     * Reveal radius in grid cells.  Server-side validation caps this to
     * USimGrid_FeatureSettings::GetSafeMaxFogRevealRadius() so a tampered client cannot
     * specify an unbounded radius.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Fog",
              meta = (ClampMin = "1"))
    int32 RevealRadiusCells = 8;

    /**
     * When true the component automatically reveals fog on each RevealIntervalSeconds tick
     * based on the owner's current world position.  Disable to drive reveals manually via
     * RevealAtCell().
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Fog")
    bool bRevealOnTick = true;

    /**
     * Minimum real-time seconds between automatic reveal ticks.  Rate-limits ServerReveal
     * RPCs so fast-moving units do not flood the server.  Only relevant when
     * bRevealOnTick is true.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "SimGrid|Fog",
              meta = (ClampMin = "0.01"))
    float RevealIntervalSeconds = 0.5f;

    // ─── Public API ──────────────────────────────────────────────────────────

    /**
     * Reveal fog at the given cell coordinate for this component's TeamId and RevealRadiusCells.
     * On authority: resolves the carrier and calls RevealRadius directly.
     * On a client: fires the Server Reliable RPC which the server validates and executes.
     */
    UFUNCTION(BlueprintCallable, Category = "SimGrid|Fog")
    void RevealAtCell(const FSeam_CellCoord& Cell);

    /**
     * Convenience wrapper: derive the owner actor's current grid cell from its world location
     * via ISeam_TileProviderRead and call RevealAtCell().  No-op if the tile provider is not
     * available or the owner has no world.
     */
    UFUNCTION(BlueprintCallable, Category = "SimGrid|Fog")
    void RevealAtOwnerPosition();

private:
    // ─── Server RPC ──────────────────────────────────────────────────────────

    /**
     * Authority-side reveal RPC.
     * Validation (ServerReveal_Validate): rejects if TeamId is invalid or
     * Radius exceeds GetSafeMaxFogRevealRadius().
     * Implementation: resolves the fog carrier for TeamId and calls RevealRadius(Centre, Radius).
     */
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerReveal(FSeam_CellCoord Centre, int32 Radius);
    bool ServerReveal_Validate(FSeam_CellCoord Centre, int32 Radius);
    void ServerReveal_Implementation(FSeam_CellCoord Centre, int32 Radius);

    // ─── Internal helpers ────────────────────────────────────────────────────

    /**
     * Convert the owner actor's current world location to a grid cell via the
     * ISeam_TileProviderRead seam resolved from the service locator.
     * Returns true and sets OutCell on success; returns false if the provider is unavailable.
     */
    bool GetOwnerCell(FSeam_CellCoord& OutCell) const;

    /** Accumulated real-time seconds toward the next auto-reveal tick. */
    float RevealAccumulator = 0.f;

    /**
     * One-frame weak cache of the tile provider object resolved from the service locator.
     * Automatically invalidated when the provider is GC'd.  Re-resolved on the next call.
     */
    mutable TWeakObjectPtr<UObject> CachedTileProviderObject;
};
