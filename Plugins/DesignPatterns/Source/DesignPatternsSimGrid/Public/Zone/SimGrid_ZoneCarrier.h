// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "Grid/Seam_GridCoord.h"
#include "Identity/Seam_EntityId.h"
#include "Zone/SimGrid_ZoneTypes.h"
#include "SimGrid_ZoneCarrier.generated.h"

/**
 * Fired (server and clients) whenever a cell's zone assignment changes — after replication on clients.
 * @param Carrier The zone carrier whose cell changed.
 * @param Cell    The affected cell.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSimGrid_OnZoneChanged,
	ASimGrid_ZoneCarrier*, Carrier, FSeam_CellCoord, Cell);

/**
 * Replicated authority carrier for the world's painted ZONES/districts.
 *
 * SimGrid subsystems are never replicated, so all authoritative zone state lives on this AInfo (mirroring
 * ASimGrid_ChunkReplicator for tiles). Zones are global-ish and far sparser than tiles, so a single
 * carrier holds the whole zone map in one delta-serialized fast array rather than one carrier per chunk.
 * Every mutator is AUTHORITY ONLY and guards HasAuthority() at the TOP; clients receive zone deltas via
 * the FFastArraySerializer and observe changes through OnZoneChanged.
 *
 * The carrier publishes itself to the service locator (under the features settings' ZoneCarrierServiceTag)
 * so painting tools and the growth component resolve it without a hard include. Net dormancy: dormant
 * until a paint changes state, so an idle zone map costs no per-frame bandwidth.
 */
UCLASS()
class DESIGNPATTERNSSIMGRID_API ASimGrid_ZoneCarrier : public AInfo
{
	GENERATED_BODY()

public:
	ASimGrid_ZoneCarrier();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void PostInitializeComponents() override;
	//~ End AActor

	// --- Authority mutators (AUTHORITY ONLY; each guards HasAuthority() at the top) ---

	/**
	 * Paint ZoneTypeTag onto Cell, owned by OwnerId. Adds or updates the entry, resets its growth to 0 on
	 * a type change, marks it dirty and notifies. AUTHORITY ONLY.
	 * @return True if a change was applied.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Zone")
	bool PaintZone(const FSeam_CellCoord& Cell, FGameplayTag ZoneTypeTag, const FSeam_EntityId& OwnerId);

	/** Erase any zone painted on Cell. AUTHORITY ONLY. @return True if an entry existed and was removed. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Zone")
	bool EraseZone(const FSeam_CellCoord& Cell);

	/**
	 * Set the growth level [0,1] of a zoned cell (clamped). AUTHORITY ONLY; called by the growth
	 * component as it advances development over sim time. @return True if the cell was zoned and changed.
	 */
	bool SetZoneGrowth(const FSeam_CellCoord& Cell, float NewGrowth);

	// --- Reads (client-safe; observe replicated state) ---

	/** The zone-type tag painted on Cell, or an invalid tag if unzoned / unknown. */
	UFUNCTION(BlueprintPure, Category = "SimGrid|Zone")
	FGameplayTag GetZoneAt(const FSeam_CellCoord& Cell) const;

	/** The growth level [0,1] of a zoned cell, or 0 if unzoned. */
	UFUNCTION(BlueprintPure, Category = "SimGrid|Zone")
	float GetZoneGrowth(const FSeam_CellCoord& Cell) const;

	/** The owner of a zoned cell, or an invalid id if unzoned / unowned. */
	UFUNCTION(BlueprintPure, Category = "SimGrid|Zone")
	FSeam_EntityId GetZoneOwner(const FSeam_CellCoord& Cell) const;

	/** Find the zone entry for Cell, or null. Const, client-safe. */
	const FSimGrid_ZoneEntry* FindEntry(const FSeam_CellCoord& Cell) const;

	/** Read-only access to all zone entries (for growth iteration / save capture). */
	const TArray<FSimGrid_ZoneEntry>& GetZoneEntries() const { return Zones.Entries; }

	/** Fired when a cell's zone assignment changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "SimGrid|Zone")
	FSimGrid_OnZoneChanged OnZoneChanged;

	/** Called by the fast-array item callbacks on clients to surface a replicated zone change. */
	void HandleReplicatedZoneChange(const FSeam_CellCoord& Cell);

	/**
	 * Resolve the authoritative zone carrier from the service locator (or null). Spawns nothing — the
	 * carrier is expected to be placed/spawned by the world. Helper for painting tools and growth.
	 */
	static ASimGrid_ZoneCarrier* Resolve(const UObject* WorldContextObject);

private:
	/** Replicated painted zones (delta-serialized). */
	UPROPERTY(Replicated)
	FSimGrid_ZoneArray Zones;

	/** Service-locator key this carrier registered under, for clean unregister on teardown. */
	UPROPERTY(Transient)
	FGameplayTag RegisteredServiceTag;

	/** Mutable find for the authority mutators; returns null if absent. */
	FSimGrid_ZoneEntry* FindEntryMutable(const FSeam_CellCoord& Cell);

	/** Wake the actor from net dormancy so a just-changed delta replicates this frame. */
	void WakeForChange();

	/** Register/unregister this carrier as the zone-carrier service. */
	void RegisterService();
	void UnregisterService();
};
