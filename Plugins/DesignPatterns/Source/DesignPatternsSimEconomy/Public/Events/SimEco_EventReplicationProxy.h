// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Info.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "GameplayTagContainer.h"
#include "SimEco_EventReplicationProxy.generated.h"

class ASimEco_EventReplicationProxy;

/** One replicated active-event summary: the event tag + remaining steps. Fast-array item. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_ActiveEventEntry : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Classification tag of the active event (for UI/notifications). */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Events")
	FGameplayTag EventTag;

	/** Steps remaining before the event ends. */
	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Events")
	int32 RemainingSteps = 0;

	FSimEco_ActiveEventEntry() = default;

	void PostReplicatedAdd(const struct FSimEco_ActiveEventArray& InArraySerializer);
	void PostReplicatedChange(const struct FSimEco_ActiveEventArray& InArraySerializer);
	void PreReplicatedRemove(const struct FSimEco_ActiveEventArray& InArraySerializer);
};

/** Fast-array of active-event summaries. */
USTRUCT(BlueprintType)
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_ActiveEventArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "SimEconomy|Events")
	TArray<FSimEco_ActiveEventEntry> Entries;

	UPROPERTY(NotReplicated, Transient)
	TObjectPtr<ASimEco_EventReplicationProxy> OwnerProxy = nullptr;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FastArrayDeltaSerialize<FSimEco_ActiveEventEntry, FSimEco_ActiveEventArray>(Entries, DeltaParms, *this);
	}
};

template<>
struct TStructOpsTypeTraits<FSimEco_ActiveEventArray> : public TStructOpsTypeTraitsBase2<FSimEco_ActiveEventArray>
{
	enum { WithNetDeltaSerializer = true };
};

/** Broadcast (server + clients) after the active-event summary changes. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSimEco_OnActiveEventsChanged, ASimEco_EventReplicationProxy*, Proxy);

/**
 * Replicated carrier for the active ECONOMIC-EVENT summary (which events are live and for how long).
 *
 * The economic-event subsystem is never replicated; it pushes the live event tags + remaining steps
 * into THIS AInfo (server-spawned). Clients read it for "Shortage!" banners and the like. The synthetic
 * market orders the events inject are server-only and reach clients only as the resulting price changes
 * (via the existing market price proxy).
 */
UCLASS(NotPlaceable, Transient)
class DESIGNPATTERNSSIMECONOMY_API ASimEco_EventReplicationProxy : public AInfo
{
	GENERATED_BODY()

public:
	ASimEco_EventReplicationProxy();

	//~ Begin AActor
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PostInitProperties() override;
	//~ End AActor

	/** AUTHORITY ONLY: set the active events (replaces the whole list, marking dirty). */
	void SetActiveEvents(const TArray<FSimEco_ActiveEventEntry>& InEntries);

	/** Copy active events out for UI. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Events")
	void GetActiveEvents(TArray<FSimEco_ActiveEventEntry>& OutEntries) const { OutEntries = Active.Entries; }

	/** Fired (server + clients) after the active-event summary changes. */
	UPROPERTY(BlueprintAssignable, Category = "SimEconomy|Events")
	FSimEco_OnActiveEventsChanged OnActiveEventsChanged;

	/** Called by the fast-array callbacks on clients. */
	void HandleReplicatedChange();

private:
	UPROPERTY(Replicated)
	FSimEco_ActiveEventArray Active;
};
