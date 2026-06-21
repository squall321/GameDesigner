// Copyright DesignPatterns plugin. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Economy/Seam_RewardSink.h"
#include "GameplayTagContainer.h"
#include "SimEco_QuestRewardComponent.generated.h"

class UDP_ServiceLocatorSubsystem;

/**
 * The economy's REWARD SINK: pays quest / achievement / encounter rewards to a player by resolving the
 * player's wallet (ISeam_WalletAuthority) and inventory (ISeam_PurchaseTarget) off the receiving actor.
 *
 * Implements ISeam_RewardSink and registers under SimEcoPricingTags::Service_RewardSink, so a quest or
 * achievement system grants a reward by resolving that one service tag and calling PayReward — instead
 * of broadcasting a "give gold" COMMAND on the message bus (the resolved anti-pattern). The bus is used
 * only AFTER the fact, to notify UI that a reward landed.
 *
 * Place this on a server-authoritative carrier (the game mode actor, a rules manager). PayReward is
 * AUTHORITY ONLY and guards HasAuthority() at the TOP. Currency lines roll back item lines on a hard
 * inventory failure so a reward is never half-paid.
 */
UCLASS(ClassGroup = (DesignPatternsSimEconomy), meta = (BlueprintSpawnableComponent))
class DESIGNPATTERNSSIMECONOMY_API USimEco_QuestRewardComponent
	: public UActorComponent
	, public ISeam_RewardSink
{
	GENERATED_BODY()

public:
	USimEco_QuestRewardComponent();

	//~ Begin UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End UActorComponent

	//~ Begin ISeam_RewardSink
	/** AUTHORITY ONLY. Pay Spec out to Receiver: currency via wallet-authority, items via purchase-target. */
	virtual bool PayReward_Implementation(AActor* Receiver, const FSeam_RewardSpec& Spec) override;
	//~ End ISeam_RewardSink

	/** Convenience: build + pay a single currency reward. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Reward")
	bool PayCurrency(AActor* Receiver, FGameplayTag SourceTag, FGameplayTag CurrencyTag, int64 Amount);

	/** Convenience: build + pay a single item reward. AUTHORITY ONLY. */
	UFUNCTION(BlueprintCallable, Category = "SimEconomy|Reward")
	bool PayItem(AActor* Receiver, FGameplayTag SourceTag, FGameplayTag ItemTag, int32 Count);

protected:
	/** True if this component's owner has authority. */
	bool HasAuthority() const;

	/** Resolve a UINTERFACE seam off the receiver (actor first, then components). */
	static UObject* ResolveSeam(const AActor* Receiver, TSubclassOf<UInterface> SeamClass);

	/** Resolve the GameInstance service locator. */
	UDP_ServiceLocatorSubsystem* ResolveLocator() const;

private:
	/** True once registered as the reward-sink service. */
	bool bRegistered = false;
};
