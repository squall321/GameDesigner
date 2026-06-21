// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DPSubsystemBases.h"
#include "Persist/Seam_Persistable.h"
#include "Economy/SimEco_StepListener.h"
#include "GameplayTagContainer.h"

#if __has_include("StructUtils/InstancedStruct.h")
#include "StructUtils/InstancedStruct.h"
#else
#include "InstancedStruct.h"
#endif

#include "SimEco_EconomicEventSubsystem.generated.h"

class USimEco_EconomicEventDef;
class USimEco_MarketSubsystem;
class USimEco_EconomySubsystem;
class ASimEco_EventReplicationProxy;

/** One live economic event: the def plus how many steps remain. Server-only runtime. */
USTRUCT()
struct FSimEco_LiveEvent
{
	GENERATED_BODY()

	/** The driving event asset. */
	UPROPERTY(Transient)
	TObjectPtr<USimEco_EconomicEventDef> Def = nullptr;

	/** Classification tag (cached from Def for the replicated summary). */
	UPROPERTY()
	FGameplayTag EventTag;

	/** Steps remaining. */
	UPROPERTY()
	int32 RemainingSteps = 0;
};

/** One persisted live-event record (by event tag + remaining steps; the def re-resolves by tag). */
USTRUCT()
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_SavedEvent
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) FGameplayTag EventTag;
	UPROPERTY(SaveGame) int32 RemainingSteps = 0;
};

/** Durable record of all live economic events. */
USTRUCT()
struct DESIGNPATTERNSSIMECONOMY_API FSimEco_EventSaveRecord
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) TArray<FSimEco_SavedEvent> SavedEvents;
};

/**
 * World-scoped, server-authoritative ECONOMIC-EVENT driver (shortages and booms).
 *
 * Implements ISimEco_StepListener: each economy step it advances every live event, INJECTS the event's
 * synthetic supply/demand into the SAME shared market (ISimEco_Market::PlaceOrder) so prices move, and
 * reaps expired events. Game logic (a director / random roller / scripted beat) triggers an event by
 * calling TriggerEvent with an event def. The active-event SUMMARY replicates via an AInfo proxy;
 * the price impact reaches clients through the existing market price proxy. Persistable.
 */
UCLASS()
class DESIGNPATTERNSSIMECONOMY_API USimEco_EconomicEventSubsystem
	: public UDP_WorldSubsystem
	, public ISimEco_StepListener
	, public ISeam_Persistable
{
	GENERATED_BODY()

public:
	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	/** UWorldSubsystem has no HasWorldAuthority of its own — declare our own. */
	bool HasWorldAuthority() const
	{
		const UWorld* W = GetWorld();
		return W && W->GetNetMode() != NM_Client;
	}

	//~ Begin ISimEco_StepListener
	/** Server-only. Inject each live event's synthetic orders, decrement, reap expired. */
	virtual void AdvanceEconomyStep(double StepSeconds, int64 StepIndex, int32 DayNumber) override;
	//~ End ISimEco_StepListener

	//~ Begin ISeam_Persistable
	virtual void CaptureState_Implementation(FInstancedStruct& Out) const override;
	virtual void RestoreState_Implementation(const FInstancedStruct& In) override;
	virtual FGameplayTag GetPersistenceKind_Implementation() const override;
	//~ End ISeam_Persistable

	//~ Begin UDP_WorldSubsystem
	virtual FString GetDPDebugString_Implementation() const override;
	//~ End UDP_WorldSubsystem

	/**
	 * Begin EventDef (a shortage / boom). AUTHORITY ONLY. Idempotent per event tag: triggering an
	 * already-live event refreshes its remaining duration. Returns true if started/refreshed.
	 */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Events")
	bool TriggerEvent(USimEco_EconomicEventDef* EventDef);

	/** End the live event with EventTag early. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Events")
	bool EndEvent(FGameplayTag EventTag);

	/** True if any event with EventTag is currently live. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Events")
	bool IsEventActive(FGameplayTag EventTag) const;

	/** The replicated active-event summary proxy. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "SimEconomy|Events")
	ASimEco_EventReplicationProxy* GetProxy() const { return Proxy.Get(); }

private:
	/** Live events (server-only). */
	UPROPERTY(Transient)
	TArray<FSimEco_LiveEvent> LiveEvents;

	/** Replicated active-event summary carrier. */
	UPROPERTY(Transient)
	TWeakObjectPtr<ASimEco_EventReplicationProxy> Proxy;

	/** True once registered with the economy driver. */
	bool bRegisteredWithEconomy = false;

	void EnsureProxy();
	USimEco_EconomySubsystem* ResolveEconomy() const;
	USimEco_MarketSubsystem* ResolveMarket() const;

	/** Push the current live-event list into the replicated proxy. */
	void SyncProxy();

	/** Inject one event's synthetic orders into the market this step. */
	void InjectEventOrders(const FSimEco_LiveEvent& Event);

	/** Notify the bus that the active-event set changed. */
	void NotifyEventsChanged() const;

	/** Re-resolve an event def by its tag from the data registry (for save-restore). */
	USimEco_EconomicEventDef* ResolveEventDefByTag(const FGameplayTag& EventTag) const;
};
