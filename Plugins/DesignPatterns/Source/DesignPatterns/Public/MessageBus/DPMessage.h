// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"

// FInstancedStruct lives in the StructUtils plugin on UE 5.3/5.4 and is merged into
// CoreUObject in 5.5+. Include the right header for the engine band.
#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "DPMessage.generated.h"

/** How a listener's subscribed tag is matched against a broadcast channel tag. */
UENUM(BlueprintType)
enum class EDP_MessageMatch : uint8
{
	/** Listener fires only when the broadcast channel is exactly its subscribed tag. */
	Exact,

	/** Listener fires when the broadcast channel is its tag OR a child of it (default). */
	ExactOrChild
};

/**
 * A single message on the bus. Native payload type — carries a wildcard FInstancedStruct
 * so new message types need zero bus changes. NOT replicated: messages are produced from
 * already-replicated state and re-broadcast locally on each machine.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_Message
{
	GENERATED_BODY()

	/** The channel this message was published on. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|MessageBus")
	FGameplayTag Channel;

	/** Strongly-typed payload (any USTRUCT). Build with the engine's Make Instanced Struct node. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|MessageBus")
	FInstancedStruct Payload;

	/** Optional originator. Weak so a destroyed instigator does not keep the message alive. */
	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|MessageBus")
	TWeakObjectPtr<UObject> Instigator;

	FDP_Message() = default;

	FDP_Message(const FGameplayTag& InChannel, const FInstancedStruct& InPayload, UObject* InInstigator = nullptr)
		: Channel(InChannel)
		, Payload(InPayload)
		, Instigator(InInstigator)
	{
	}
};

/**
 * Opaque handle to a registered listener. Returned by Listen*, passed back to StopListening.
 * Comparable and hashable so it can live in containers.
 */
USTRUCT(BlueprintType)
struct DESIGNPATTERNS_API FDP_ListenerHandle
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "DesignPatterns|MessageBus")
	int64 Id = 0;

	bool IsValid() const { return Id != 0; }

	bool operator==(const FDP_ListenerHandle& Other) const { return Id == Other.Id; }
	bool operator!=(const FDP_ListenerHandle& Other) const { return Id != Other.Id; }

	friend uint32 GetTypeHash(const FDP_ListenerHandle& H) { return GetTypeHash(H.Id); }
};

/**
 * Blueprint-assignable listener signature. Params are passed by value-friendly types only —
 * Channel + Payload + Instigator are split out so no TWeakObjectPtr or whole FDP_Message
 * crosses the BP delegate boundary (which the critique flagged as fragile).
 */
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FDP_MessageDynamicDelegate,
	FGameplayTag, Channel,
	FInstancedStruct, Payload,
	UObject*, Instigator);
