// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GameplayTagContainer.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "DPGameplayActionInterface.generated.h"

class AActor;
class UDP_GameplayActionComponent;

/**
 * Data threaded into an action when it activates. Mirrors GAS's activation info but with zero
 * GAS dependency: who owns the action, who triggered it, and an optional typed payload.
 *
 * Kept a plain USTRUCT carrying weak object refs (non-owning) and an FInstancedStruct payload,
 * so it is cheap to pass by const-ref and never participates in GC ownership of the targets.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_ActionActivationData
{
	GENERATED_BODY()

	/** The component that owns/grants the action being activated. Non-owning. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|Action")
	TWeakObjectPtr<UDP_GameplayActionComponent> SourceComponent;

	/** The actor that instigated activation (presser/AI). Non-owning. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|Action")
	TWeakObjectPtr<AActor> Instigator;

	/** Optional target actor for targeted actions. Non-owning. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|Action")
	TWeakObjectPtr<AActor> Target;

	/** Optional typed payload (e.g. aim location, charge level). Built via Make Instanced Struct. */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|Action")
	FInstancedStruct Payload;

	/** Free-form context tags forwarded to the action (e.g. input source, combo state). */
	UPROPERTY(BlueprintReadWrite, Category = "DesignPatterns|Action")
	FGameplayTagContainer ContextTags;

	FDP_ActionActivationData() = default;
};

/** UINTERFACE boilerplate for IDP_GameplayAction. */
UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UDP_GameplayAction : public UInterface
{
	GENERATED_BODY()
};

/**
 * Minimal contract every lightweight action satisfies. Lets systems treat actions polymorphically
 * (query identity, ask if they can run) without depending on the concrete UDP_GameplayActionLite
 * base — handy for adapters and the opt-in GAS bridge module.
 */
class DESIGNPATTERNS_API IDP_GameplayAction
{
	GENERATED_BODY()

public:
	/** The tag identifying this action (used for granting, lookup and cooldown bookkeeping). */
	virtual FGameplayTag GetActionTag() const = 0;

	/** Lightweight predicate: may this action activate given the supplied activation context? */
	virtual bool CanActivateAction(const FDP_ActionActivationData& Data) const = 0;
};
