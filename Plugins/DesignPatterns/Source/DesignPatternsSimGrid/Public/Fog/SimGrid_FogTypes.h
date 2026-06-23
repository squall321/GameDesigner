// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "SimGrid_FogTypes.generated.h"

// Forward declarations
class ASimGrid_FogCarrier;

// ─────────────────────────────────────────────────────────────────────────────
// Delegate — broadcast when fog state changes for a specific cell
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Broadcast on both server and clients whenever fog state changes for a cell.
 * @param Carrier  The fog carrier whose state changed.
 * @param Cell     The cell whose visibility state changed.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
    FSimGrid_OnFogChanged,
    ASimGrid_FogCarrier*, Carrier,
    FSeam_CellCoord,      Cell
);

// ─────────────────────────────────────────────────────────────────────────────
// FSimGrid_FogRun
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A single run-length-encoded horizontal span of fog-of-war state.
 *
 * Rather than storing one entry per cell, spans of revealed cells on the same
 * row are collapsed into a single FSimGrid_FogRun.  This substantially reduces
 * the number of items serialised over the network for typical reveal patterns
 * (circles, rectangles).
 *
 * bCurrentlyVisible distinguishes two render states:
 *   true  — bright / fully visible: the unit can see this cell right now.
 *   false — explored / "shroud":    the cell was seen at some point but is
 *           currently out of line-of-sight (dim overlay).
 *
 * Explored state is permanent; currently-visible state is ephemeral and is
 * cleared each turn (or whenever ConcealAll is called).
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_FogRun : public FFastArraySerializerItem
{
    GENERATED_BODY()

    /** Row (Y-axis grid coordinate) that this span occupies. */
    UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Fog")
    int32 RowY = 0;

    /** Inclusive start X of the revealed span on RowY. */
    UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Fog")
    int32 StartX = 0;

    /** Inclusive end X of the revealed span on RowY. */
    UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Fog")
    int32 EndX = 0;

    /**
     * When true the span is currently visible (bright); when false it has been
     * explored but is no longer in line-of-sight (dim / shroud).
     */
    UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Fog")
    bool bCurrentlyVisible = false;

    //~ FFastArraySerializerItem replication callbacks — invoked on clients only.
    void PreReplicatedRemove(const struct FSimGrid_FogRunArray& InArraySerializer);
    void PostReplicatedAdd(const struct FSimGrid_FogRunArray& InArraySerializer);
    void PostReplicatedChange(const struct FSimGrid_FogRunArray& InArraySerializer);
};

// ─────────────────────────────────────────────────────────────────────────────
// FSimGrid_FogRunArray
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Fast-array container for a set of FSimGrid_FogRun entries.
 *
 * Used twice per ASimGrid_FogCarrier: once for ExploredRuns (every cell the
 * team has ever seen) and once for VisibleRuns (cells visible right now,
 * populated only when USimGrid_FeatureSettings::bTrackCurrentVisibility is
 * true).
 *
 * OwnerCarrier is transient and not replicated; it is set in
 * ASimGrid_FogCarrier::BeginPlay so that fast-array callbacks can forward
 * change notifications to the carrier.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMGRID_API FSimGrid_FogRunArray : public FFastArraySerializer
{
    GENERATED_BODY()

    /** All fog runs managed by this array. */
    UPROPERTY(BlueprintReadOnly, Category = "SimGrid|Fog")
    TArray<FSimGrid_FogRun> Entries;

    /**
     * Non-replicated back-pointer to the owning carrier so that fast-array
     * callbacks can forward change notifications.  Set on both server and
     * clients after the carrier is initialised.
     */
    UPROPERTY(NotReplicated, Transient)
    TObjectPtr<ASimGrid_FogCarrier> OwnerCarrier = nullptr;

    /**
     * Standard net-delta serialization entry point required by
     * TStructOpsTypeTraits<FSimGrid_FogRunArray>.
     */
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
    {
        return FastArrayDeltaSerialize<FSimGrid_FogRun, FSimGrid_FogRunArray>(
            Entries, DeltaParms, *this);
    }
};

/**
 * Opt-in trait that tells the engine this struct supports net-delta
 * serialization via FFastArraySerializer.
 */
template<>
struct TStructOpsTypeTraits<FSimGrid_FogRunArray>
    : public TStructOpsTypeTraitsBase2<FSimGrid_FogRunArray>
{
    enum { WithNetDeltaSerializer = true };
};
