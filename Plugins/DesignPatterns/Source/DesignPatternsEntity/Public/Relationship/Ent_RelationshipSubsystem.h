// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Identity/Seam_EntityRelationship.h"
#include "MessageBus/DPMessage.h"
#include "Relationship/Ent_EntityLinkArray.h"
#include "Ent_RelationshipSubsystem.generated.h"

class UEnt_RelationshipComponent;

/**
 * World-scoped bidirectional relationship index, keyed by FSeam_EntityId, implementing the
 * cross-cutting read seam ISeam_EntityRelationshipRead.
 *
 * Built locally on server AND clients from the replicated relationship components (exactly like the
 * entity registry), so every machine has a consistent local graph keyed by the replicated entity id.
 * Relationship components register/unregister their links here on BeginPlay/OnRep/EndPlay.
 *
 * QUERIES
 *  - outgoing/incoming links by kind, children-of, owner-chain walk (cycle-guarded), group members.
 *
 * AUTHORITY
 *  Indexing runs on all machines. Lifetime PROPAGATION (destroy-children-with-parent) is authority-only
 *  and gated on this subsystem's OWN authority helper (GetNetMode() != NM_Client) — there is no base
 *  helper. It listens to FEnt_EntityLifecycleMessage on the bus so it can prune dead links and cascade
 *  destruction when an owning entity unregisters.
 *
 * NOT replicated (subsystems never are).
 */
UCLASS()
class DESIGNPATTERNSENTITY_API UEnt_RelationshipSubsystem
	: public UDP_WorldSubsystem
	, public ISeam_EntityRelationshipRead
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** True on server / standalone, false on a network client. Our own authority check (no base helper). */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	// ---- Registration (called by UEnt_RelationshipComponent) -----------------------------------

	/** Register a single outgoing link From -> Link.TargetId of Link.LinkKindTag. Idempotent. */
	void RegisterLink(FSeam_EntityId From, const FEnt_EntityLink& Link);

	/** Unregister a single outgoing link From -> TargetId of KindTag. */
	void UnregisterLink(FSeam_EntityId From, FSeam_EntityId TargetId, FGameplayTag KindTag);

	/** Drop every outgoing link recorded for From (e.g. when its component is torn down). */
	void UnregisterAllFrom(FSeam_EntityId From);

	// ---- Graph queries -------------------------------------------------------------------------

	/** Entities that name Source as a link target of KindTag (the "children" of Source under that kind). */
	UFUNCTION(BlueprintCallable, Category = "Entity|Relationship")
	void GetChildren(FSeam_EntityId Source, FGameplayTag KindTag, TArray<FSeam_EntityId>& Out) const;

	/**
	 * Walk the owner chain upward from Source (following Ent.Link.Owner) into Out, nearest owner first.
	 * Cycle-guarded and capped at MaxOwnerChainDepth. Source itself is NOT included.
	 */
	UFUNCTION(BlueprintCallable, Category = "Entity|Relationship")
	void GetOwnerChain(FSeam_EntityId Source, TArray<FSeam_EntityId>& Out) const;

	/** Members of Source's group: targets of Source's Ent.Link.Grouped links (plus the reverse). */
	UFUNCTION(BlueprintCallable, Category = "Entity|Relationship")
	void GetGroupMembers(FSeam_EntityId Source, TArray<FSeam_EntityId>& Out) const;

	//~ Begin ISeam_EntityRelationshipRead
	virtual bool HasRelationshipIndex() const override;
	virtual int32 GetLinkedEntities(FSeam_EntityId Source, FGameplayTag LinkKindTag, TArray<FSeam_EntityId>& Out) const override;
	virtual FSeam_EntityId GetPrimaryOwner(FSeam_EntityId Source) const override;
	virtual bool AreLinked(FSeam_EntityId A, FSeam_EntityId B, FGameplayTag LinkKindTag) const override;
	//~ End ISeam_EntityRelationshipRead

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/** Hard cap on owner-chain depth so a malformed graph cannot spin forever (tunable). */
	UPROPERTY(EditAnywhere, Category = "Entity|Relationship", meta = (ClampMin = "1"))
	int32 MaxOwnerChainDepth = 32;

private:
	/** A directed edge stored in the forward index (Source -> these). */
	struct FOutgoing
	{
		FSeam_EntityId Target;
		FGameplayTag Kind;
	};

	/** Forward index: source id -> its outgoing edges. */
	TMap<FSeam_EntityId, TArray<FOutgoing>> Outgoing;

	/** Reverse index: target id -> sources that link to it (for children/group queries). */
	TMap<FSeam_EntityId, TArray<FOutgoing>> Incoming;

	/** Bus listener handle for entity-lifecycle pruning. Removed in Deinitialize. */
	FDP_ListenerHandle LifecycleListenerHandle;

	/** Authority-only: when an owning entity unregisters, prune/cascade per the child policy. */
	void HandleEntityLifecycle(const FDP_Message& Message);

	/** Authority-only: destroy entities that name ParentId as their primary owner (if they opt in). */
	void DestroyChildrenOf(FSeam_EntityId ParentId);
};
