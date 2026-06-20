// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "UObject/ScriptInterface.h"
#include "Grid/Seam_GridCoord.h"
#include "Placement/SimGrid_PlacementTypes.h"
#include "SimGrid_PlacementComponent.generated.h"

class USimGrid_PlacementRuleStrategy;
class ISeam_TileProviderRead;
class ISimGrid_GhostPreview;
class ISimGrid_Placeable;

/**
 * Broadcast on the requesting client and the server after a placement is committed (or rejected).
 * @param bSuccess   True if the server committed the placement.
 * @param Origin     The origin cell the placement was committed at.
 * @param Rotation   The committed rotation.
 * @param Reason     On failure, the first failing rule's reason tag; invalid on success.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FSimGrid_OnPlacementCommitted, bool, bSuccess,
	FSeam_CellCoord, Origin, ESimGrid_Rotation, Rotation, FGameplayTag, Reason);

/**
 * Player-owned driver for placing things on the SimGrid: ghost preview, client-side validation, and the
 * authoritative client->server commit handshake.
 *
 * Lives on a PLAYER-CONTROLLED actor (the pawn/controller). The local client runs ValidatePlacement
 * (pure, runs the rule strategies against the read seam) to drive the ghost; when the player confirms,
 * RequestPlacement fires a Server RPC (ServerCommitPlacement, Reliable, WithValidation). The server
 * RE-DERIVES the placeable's footprint and RE-RUNS the rules authoritatively — it never trusts a
 * client-supplied footprint or validity — then resolves the grid and territory carriers through the
 * read seam + service locator and applies the change authority-only.
 *
 * The component holds NO replicated state of its own; authoritative grid/ownership state lives on the
 * resolved carriers. The ghost is a purely client-side cosmetic seam.
 */
UCLASS(ClassGroup = (DesignPatternsSimGrid), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMGRID_API USimGrid_PlacementComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USimGrid_PlacementComponent();

	//~ Begin UActorComponent
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	// --- Configuration ---

	/**
	 * The active placeable being placed. Set when the player begins a placement; the component reads its
	 * footprint/terrain requirements for validation and previewing. Non-owning (the placeable is owned by
	 * whatever spawned/holds it); null-checked before use.
	 */
	UPROPERTY(BlueprintReadWrite, Category = "SimGrid|Placement")
	TScriptInterface<ISimGrid_Placeable> ActivePlaceable;

	/**
	 * Ordered rule strategies applied during validation, in addition to any rules carried by the
	 * placeable. Instanced and editable inline so designers compose a default placement policy on the
	 * component (e.g. InBounds + CellsEmpty). Evaluated client-side for the ghost and re-evaluated
	 * server-side for the commit.
	 */
	UPROPERTY(EditAnywhere, Instanced, BlueprintReadOnly, Category = "SimGrid|Placement")
	TArray<TObjectPtr<USimGrid_PlacementRuleStrategy>> Rules;

	// --- Ghost preview (client-side, cosmetic) ---

	/** Assign the ghost-preview object the component drives while placing. Pass an empty value to clear. */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Placement")
	void SetGhost(const TScriptInterface<ISimGrid_GhostPreview>& InGhost);

	/**
	 * Re-validate at Origin/Rotation and refresh the ghost accordingly. Call as the player moves the
	 * cursor. Pure-ish on authoritative state (only touches the cosmetic ghost). Returns the result so
	 * UI can also read it.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Placement")
	FSimGrid_PlacementResult UpdateGhost(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation);

	/** Hide the ghost (placement cancelled / cursor off grid). */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Placement")
	void CancelPlacement();

	// --- Validation (pure, client-safe) ---

	/**
	 * Run the full rule set (component Rules + the placeable's required tile types) against a freshly
	 * built FSimGrid_PlacementContext, returning the aggregate result. Pure and client-safe: queries the
	 * grid only through the read seam, mutates nothing. The SAME path is used by the server commit.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimGrid|Placement")
	FSimGrid_PlacementResult ValidatePlacement(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation) const;

	// --- Commit handshake ---

	/**
	 * Player intent to commit the active placement. On a client this validates locally first (to avoid
	 * obviously-doomed RPCs) and, if not Invalid, routes to the server via ServerCommitPlacement. On a
	 * listen-server/standalone host this commits directly. Does nothing without an ActivePlaceable.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimGrid|Placement")
	void RequestPlacement(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation);

	/** Fired (client + server) after a commit attempt resolves. */
	UPROPERTY(BlueprintAssignable, Category = "SimGrid|Placement")
	FSimGrid_OnPlacementCommitted OnPlacementCommitted;

	/**
	 * Resolve the read-only grid seam this component places onto. Resolves via the service locator key
	 * SimGridTags::Service_TileProvider. Returns an empty interface if unavailable.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimGrid|Placement")
	TScriptInterface<ISeam_TileProviderRead> ResolveGrid() const;

protected:
	/**
	 * Server RPC: authoritatively commit a placement. The server RE-DERIVES the footprint from the
	 * placeable and RE-RUNS the rules; it ignores any client-claimed validity. PlaceableObject is the
	 * UObject implementing ISimGrid_Placeable (validated to be owned/relevant to this player on the
	 * server). Reliable so a confirmed placement is never silently dropped.
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerCommitPlacement(UObject* PlaceableObject, FSeam_CellCoord Origin, ESimGrid_Rotation Rotation);

	/** Multicast back to the requesting client's component to surface the commit outcome. */
	UFUNCTION(Client, Reliable)
	void ClientNotifyPlacementResult(bool bSuccess, FSeam_CellCoord Origin, ESimGrid_Rotation Rotation, FGameplayTag Reason);

private:
	/** The ghost-preview object driven while placing (client-side cosmetic). Non-owning. */
	UPROPERTY(Transient)
	TScriptInterface<ISimGrid_GhostPreview> Ghost;

	/**
	 * Build the placement context for Origin/Rotation: resolves the grid, gathers the placeable's
	 * footprint, and fills OwnerId from the owning actor's identity convention. Returns false if no grid
	 * or no active placeable.
	 */
	bool BuildContext(const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation, FSimGrid_PlacementContext& OutContext) const;

	/** Collect the effective rule list for validation: component Rules plus a synthesized terrain rule. */
	void GatherEffectiveRules(const FSimGrid_PlacementContext& Context, TArray<const USimGrid_PlacementRuleStrategy*>& OutRules) const;

	/** Aggregate a context through a rule list into a full FSimGrid_PlacementResult. Pure. */
	static FSimGrid_PlacementResult EvaluateRules(const FSimGrid_PlacementContext& Context,
		const TArray<const USimGrid_PlacementRuleStrategy*>& InRules);

	/**
	 * Authoritative commit body shared by the host path and ServerCommitPlacement. Re-derives, re-checks,
	 * resolves carriers, applies, and notifies. Returns the authoritative result.
	 */
	FSimGrid_PlacementResult CommitAuthoritative(UObject* PlaceableObject, const FSeam_CellCoord& Origin, ESimGrid_Rotation Rotation);

	/** The OwnerId tag for placements by this component's owner (from ISeam_EntityIdentity if present). */
	FGameplayTag DeriveOwnerId() const;

	/** True iff this component's owner has network authority. */
	bool HasOwnerAuthority() const;
};
