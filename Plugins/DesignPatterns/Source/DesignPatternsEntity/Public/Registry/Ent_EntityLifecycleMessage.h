// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Identity/Seam_EntityId.h"
#include "Ent_EntityLifecycleMessage.generated.h"

/** Which lifecycle transition a lifecycle message describes. */
UENUM(BlueprintType)
enum class EEnt_EntityLifecyclePhase : uint8
{
	/** The entity registered with the registry (BeginPlay on authority/clients). */
	Registered,

	/** The entity unregistered from the registry (EndPlay / destruction). */
	Unregistered
};

/**
 * Message-bus payload broadcast by the entity registry when an entity registers or unregisters.
 *
 * Carried inside an FInstancedStruct on the DesignPatterns message bus so any system can observe
 * entity lifecycle generically (by stable id + archetype) without depending on the Entity module's
 * concrete component type. Published on a channel under the bus root (see the registry's channel
 * tag); listeners filter by Phase / ArchetypeTag as needed.
 *
 * This is NOT a replicated type — it is produced locally on each machine from already-replicated
 * state (the entity component's BeginPlay/EndPlay run on every machine that has the actor), so the
 * message is re-broadcast locally and never crosses the wire.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSENTITY_API FEnt_EntityLifecycleMessage
{
	GENERATED_BODY()

	/** Whether the entity registered or unregistered. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Lifecycle")
	EEnt_EntityLifecyclePhase Phase = EEnt_EntityLifecyclePhase::Registered;

	/** The net/save-stable id of the entity this message concerns. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Lifecycle")
	FSeam_EntityId EntityId;

	/** The archetype tag of the entity (what kind of thing it is), for generic branching. */
	UPROPERTY(BlueprintReadOnly, Category = "Entity|Lifecycle")
	FGameplayTag ArchetypeTag;

	FEnt_EntityLifecycleMessage() = default;

	FEnt_EntityLifecycleMessage(EEnt_EntityLifecyclePhase InPhase, const FSeam_EntityId& InId, const FGameplayTag& InArchetype)
		: Phase(InPhase)
		, EntityId(InId)
		, ArchetypeTag(InArchetype)
	{
	}
};
