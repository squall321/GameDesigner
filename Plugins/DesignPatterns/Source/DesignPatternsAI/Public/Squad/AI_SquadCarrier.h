// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Squad/AI_SquadTypes.h"
#include "AI_SquadCarrier.generated.h"

/**
 * Fired (server and clients) whenever this squad's roster changes — after replication on clients. Carries
 * the affected member id (invalid for squad-wide changes such as the anchor moving).
 * @param Carrier  The squad carrier whose state changed.
 * @param MemberId The affected member (invalid for whole-squad changes).
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAI_OnSquadChanged,
	AAI_SquadCarrier*, Carrier, FSeam_EntityId, MemberId);

/**
 * Replicated authority carrier for ONE tactical squad.
 *
 * AI subsystems are never replicated; all authoritative squad state (member roster, per-member role and
 * formation slot, and the squad's world anchor) lives on this AInfo. The squad subsystem spawns one
 * carrier per squad on the server and routes every mutation through this actor's authority-guarded
 * mutators. Clients receive member deltas via the FFastArraySerializer and observe changes through
 * OnSquadChanged. Net dormancy: the carrier sits DORMANT_Initial and only flushes dormancy when state
 * actually changes, so a static squad costs no per-frame replication bandwidth.
 *
 * The carrier deliberately holds NO behaviour — it is pure replicated state. Coordination logic lives in
 * the subsystem, which reads this carrier and the seams.
 */
UCLASS()
class DESIGNPATTERNSAI_API AAI_SquadCarrier : public AInfo
{
	GENERATED_BODY()

public:
	AAI_SquadCarrier();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitializeComponents() override;
	//~ End AActor

	// ---- Identity (assigned once on the authority right after spawn) ----

	/**
	 * Assign this carrier's stable squad id and optional tactical/faction tag. AUTHORITY ONLY; intended
	 * to be called exactly once by the subsystem immediately after spawn (before replication seeds
	 * clients). Re-assigning logs a warning and is ignored.
	 */
	void InitSquad(const FGuid& InSquadId, const FGameplayTag& InSquadTag);

	/** The squad's stable id (replicated). Invalid until InitSquad runs on the authority. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Squad")
	FGuid GetSquadId() const { return SquadId; }

	/** The squad's tactical/faction tag (replicated). May be empty. */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Squad")
	FGameplayTag GetSquadTag() const { return SquadTag; }

	// ---- Authority mutators (each early-returns on clients) ----

	/**
	 * Add Member to the roster (no-op if already present or MaxMembers reached). AUTHORITY ONLY.
	 * @return true if the member was added.
	 */
	bool AddMember(const FSeam_EntityId& Member);

	/**
	 * Remove Member from the roster. AUTHORITY ONLY. @return true if a member was removed.
	 */
	bool RemoveMember(const FSeam_EntityId& Member);

	/**
	 * Assign Member the role tag Role (claiming it). AUTHORITY ONLY. If bExclusive, any other member
	 * currently holding Role is first cleared, so a role like Leader stays unique.
	 * @return true if Member is in the squad and the role was set.
	 */
	bool ClaimRole(const FSeam_EntityId& Member, const FGameplayTag& Role, bool bExclusive);

	/**
	 * Assign Member's formation slot (relative to the squad anchor). AUTHORITY ONLY.
	 * @return true if Member is in the squad and the slot was set.
	 */
	bool AssignSlot(const FSeam_EntityId& Member, const FTransform& RelativeSlot);

	/**
	 * Set the squad's world anchor transform (the formation origin). AUTHORITY ONLY. Members' absolute
	 * slots are this anchor composed with their RelativeSlot.
	 */
	void SetAnchorTransform(const FTransform& InAnchor);

	// ---- Reads (safe on clients; observe replicated state) ----

	/** Find the member row for Member, or null. Const, client-safe. */
	const FAI_SquadMember* FindMember(const FSeam_EntityId& Member) const;

	/** Read-only access to the full roster. */
	const TArray<FAI_SquadMember>& GetMembers() const { return Roster.Members; }

	/** Current member count. Client-safe. */
	int32 GetMemberCount() const { return Roster.Members.Num(); }

	/** The squad's replicated world anchor transform (formation origin). */
	UFUNCTION(BlueprintPure, Category = "DesignPatterns|AI|Squad")
	FTransform GetAnchorTransform() const { return AnchorTransform; }

	/** Role currently held by Member, or empty tag if absent. */
	FGameplayTag GetRole(const FSeam_EntityId& Member) const;

	/** Absolute (world) formation slot for Member = anchor * relative slot, or anchor if no slot. */
	FTransform GetAbsoluteSlot(const FSeam_EntityId& Member) const;

	/** Maximum members this carrier accepts (seeded by the subsystem from settings). */
	int32 GetMaxMembers() const { return MaxMembers; }

	/** Set the member cap (AUTHORITY ONLY; seeded once by the subsystem at spawn). */
	void SetMaxMembers(int32 InMax);

	/** Fired when any roster/anchor state changes (server and clients). */
	UPROPERTY(BlueprintAssignable, Category = "DesignPatterns|AI|Squad")
	FAI_OnSquadChanged OnSquadChanged;

	/** Called by the fast-array item callbacks on clients to surface a replicated member change. */
	void HandleReplicatedMemberChange(const FSeam_EntityId& Member);

private:
	/** Replicated stable squad id. */
	UPROPERTY(Replicated)
	FGuid SquadId;

	/** Replicated tactical/faction tag. */
	UPROPERTY(Replicated)
	FGameplayTag SquadTag;

	/** Replicated member cap. */
	UPROPERTY(Replicated)
	int32 MaxMembers = 0;

	/** Replicated world anchor (formation origin). Uses OnRep so clients fire OnSquadChanged on move. */
	UPROPERTY(ReplicatedUsing = OnRep_AnchorTransform)
	FTransform AnchorTransform = FTransform::Identity;

	/** Replicated roster (delta-serialized fast array). */
	UPROPERTY(Replicated)
	FAI_SquadMemberArray Roster;

	/** Client OnRep for the anchor: surface a whole-squad change so movers re-read absolute slots. */
	UFUNCTION()
	void OnRep_AnchorTransform();

	/** Mutable find for the authority mutators; returns null if absent. */
	FAI_SquadMember* FindMemberMutable(const FSeam_EntityId& Member);

	/** Wake the actor from net dormancy so a just-changed delta replicates this frame. */
	void WakeForChange();
};
