// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "AI_SquadTypes.generated.h"

class AAI_SquadCarrier;

/**
 * One replicated squad member. Wrapped as a fast-array item so individual joins / role changes / slot
 * assignments delta-replicate instead of resending the whole roster. All members are plain replicable
 * value types (FSeam_EntityId / FGameplayTag / FTransform) — NO FInstancedStruct, per the net rules.
 *
 * The formation slot is stored RELATIVE to the squad anchor so it stays valid as the squad moves; the
 * subsystem composes it with the live anchor transform when answering IAI_Squad::GetFormationSlot.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_SquadMember : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Stable id of the member. The roster key; never invalid for a live row. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Squad")
	FSeam_EntityId MemberId;

	/** Designer/game tactical role tag (e.g. AI.Role.Leader). Empty until a role is claimed/assigned. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Squad")
	FGameplayTag Role;

	/** Formation slot relative to the squad anchor. Identity until a slot is assigned. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Squad")
	FTransform RelativeSlot = FTransform::Identity;

	FAI_SquadMember() = default;
	explicit FAI_SquadMember(const FSeam_EntityId& InId) : MemberId(InId) {}

	//~ FFastArraySerializerItem replication callbacks (client side only).
	void PreReplicatedRemove(const struct FAI_SquadMemberArray& InArraySerializer);
	void PostReplicatedAdd(const struct FAI_SquadMemberArray& InArraySerializer);
	void PostReplicatedChange(const struct FAI_SquadMemberArray& InArraySerializer);
};

/**
 * Fast-array serializer holding a squad's members. NetDeltaSerialize forwards to FastArrayDeltaSerialize
 * so only changed members cross the wire. The owning-carrier back-pointer is non-replicated and set on
 * both server and client so per-item callbacks can notify it.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSAI_API FAI_SquadMemberArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Replicated squad members. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|AI|Squad")
	TArray<FAI_SquadMember> Members;

	/** Non-replicated back-pointer to the owning carrier, for change notifications. */
	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<AAI_SquadCarrier> OwnerCarrier = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FAI_SquadMember, FAI_SquadMemberArray>(Members, DeltaParms, *this);
	}
};

/** Enables NetDeltaSerialize for the squad member array. */
template<>
struct TStructOpsTypeTraits<FAI_SquadMemberArray> : public TStructOpsTypeTraitsBase2<FAI_SquadMemberArray>
{
	enum { WithNetDeltaSerializer = true };
};
