// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Seams/AI_Squad.h"
#include "AI_SquadSubsystem.generated.h"

class AAI_SquadCarrier;
class UDP_ServiceLocatorSubsystem;
class UAI_FormationDataAsset;

/**
 * World-scoped coordinator for tactical squads. Implements IAI_Squad so other systems read squad
 * roles/slots through the seam (resolved from DP.Service.AI.Squad) without depending on this class.
 *
 * Responsibilities:
 *   - Create / dissolve squads. On the AUTHORITY it spawns one AAI_SquadCarrier (an AInfo) per squad to
 *     hold the replicated roster; clients discover carriers as they replicate in and index them by id.
 *   - Coordinate members by the FSeam_EntityId seam: add/remove, claim roles, and lay out a fallback
 *     grid formation (spacing/columns from settings) when no explicit formation is supplied.
 *   - Share a per-squad scratch blackboard through the World hub (an Entity-scoped UWorldHub blackboard
 *     keyed by the squad id), so squad behaviour can stash coordination values that survive on the hub.
 *
 * IAI_Squad note: the seam is single-squad shaped (GetSquadId/GetRole/GetFormationSlot). This subsystem
 * answers it for its "active" squad — the most recently formed (or explicitly selected) one — while the
 * richer multi-squad API is exposed directly. Consumers that manage many squads use the direct API.
 */
UCLASS()
class DESIGNPATTERNSAI_API UAI_SquadSubsystem : public UDP_WorldSubsystem, public IAI_Squad
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/**
	 * UWorldSubsystem has no HasWorldAuthority(); declare our own. True on server / standalone /
	 * listen-server host (any net mode that is not a pure client). Squad CREATION gates on this.
	 */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	// ---- IAI_Squad (answers for the active squad) ---------------------------------------------
	virtual FGuid GetSquadId() const override;
	virtual FGameplayTag GetRole(FSeam_EntityId Member) const override;
	virtual FTransform GetFormationSlot(FSeam_EntityId Member) const override;
	virtual void GetMembers(TArray<FSeam_EntityId>& OutMembers) const override;

	// ---- Squad lifecycle (AUTHORITY ONLY for the spawn; reads are client-safe) ----------------

	/**
	 * Create a new squad with an optional tactical tag and anchor, spawning its replicated carrier.
	 * AUTHORITY ONLY. The newly-formed squad becomes the active squad. @return the new squad id (invalid
	 * on clients or on spawn failure).
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	FGuid FormSquad(FGameplayTag SquadTag, FTransform Anchor, int32 MaxMembers = 0);

	/**
	 * Dissolve a squad, destroying its carrier and clearing its shared blackboard. AUTHORITY ONLY.
	 * @return true if the squad existed and was dissolved.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	bool DissolveSquad(FGuid SquadId);

	/** Select which squad this subsystem answers IAI_Squad for. @return true if SquadId is known. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	bool SetActiveSquad(FGuid SquadId);

	// ---- Membership / roles (AUTHORITY ONLY) --------------------------------------------------

	/** Add Member to SquadId. AUTHORITY ONLY. @return true on success. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	bool AddMember(FGuid SquadId, FSeam_EntityId Member);

	/** Remove Member from SquadId. AUTHORITY ONLY. @return true on success. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	bool RemoveMember(FGuid SquadId, FSeam_EntityId Member);

	/** Claim Role for Member in SquadId (bExclusive keeps the role unique). AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	bool ClaimRole(FGuid SquadId, FSeam_EntityId Member, FGameplayTag Role, bool bExclusive = true);

	/**
	 * Re-lay out SquadId's formation: assign every member a relative grid slot using the fallback
	 * spacing/columns from settings, then push the anchor so absolute slots update. AUTHORITY ONLY.
	 * Call after membership changes to keep the formation tidy. @return true if the squad exists.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	bool RebuildFormation(FGuid SquadId);

	/** Move SquadId's anchor (formation origin). AUTHORITY ONLY. @return true if the squad exists. */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	bool SetSquadAnchor(FGuid SquadId, FTransform Anchor);

	/**
	 * Assign a designer FORMATION asset to SquadId so RebuildFormation lays members out by the asset's
	 * shape instead of the fallback grid. AUTHORITY ONLY. Pass null to revert to the fallback grid. The
	 * asset is held only here (subsystem-side) — never replicated onto the carrier. @return true if the
	 * squad exists. Re-lays out the formation immediately.
	 */
	UFUNCTION(BlueprintCallable, Category = "DesignPatterns|AI|Squad")
	bool AssignFormationAsset(FGuid SquadId, UAI_FormationDataAsset* Formation);

	// ---- Reads (client-safe) ------------------------------------------------------------------

	/** Resolve the carrier for SquadId (by querying the world for live carriers), or null. */
	AAI_SquadCarrier* FindCarrier(FGuid SquadId) const;

	/** Number of squads currently known to this subsystem (live carriers in the world). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Squad")
	int32 GetSquadCount() const;

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

private:
	/**
	 * Live carriers indexed by squad id. Held as weak pointers (carriers are owned by the world, not by
	 * this subsystem) and rebuilt/pruned lazily so a destroyed carrier simply drops out.
	 */
	UPROPERTY(Transient)
	TMap<FGuid, TWeakObjectPtr<AAI_SquadCarrier>> Carriers;

	/** The squad IAI_Squad answers for. Defaults to the most recently formed squad. */
	FGuid ActiveSquadId;

	/** Self-register under DP.Service.AI.Squad (WeakObserved) so consumers resolve IAI_Squad by tag. */
	void RegisterSelfAsService();

	/** Resolve the service locator (GameInstance-scoped), or null. */
	UDP_ServiceLocatorSubsystem* GetLocator() const;

	/**
	 * Refresh the Carriers index from the world's live AAI_SquadCarrier actors (so clients pick up
	 * carriers that replicated in, and stale entries are pruned). Const because it mutates only the
	 * transient index.
	 */
	void RebuildCarrierIndex() const;

	/**
	 * Mirror the squad's roster into a per-squad World-hub blackboard (Entity-scoped on the squad id) so
	 * coordination values share the hub. AUTHORITY path only; safe no-op if the hub is absent.
	 */
	void SyncSquadBlackboard(const AAI_SquadCarrier& Carrier);

	/** Clear the per-squad World-hub blackboard for SquadId (on dissolve). AUTHORITY path only. */
	void ClearSquadBlackboard(const FGuid& SquadId);

	/** Compute the relative grid slot for member index Index of a squad of Count members. */
	FTransform ComputeGridSlot(int32 Index, int32 Count) const;

	/**
	 * Compute the relative slot for member Index using the formation asset assigned to SquadId, or fall
	 * back to ComputeGridSlot when no asset is assigned. Additive helper — leaves the grid math untouched.
	 */
	FTransform ComputeAssetSlot(const FGuid& SquadId, int32 Index, int32 Count) const;

	/** Per-squad assigned formation assets (subsystem-side only; never replicated onto the carrier). */
	UPROPERTY(Transient)
	TMap<FGuid, TObjectPtr<UAI_FormationDataAsset>> FormationAssets;
};
